/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests walking process snapshots of threads, handles and the address space
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <processsnapshot.h>

typedef DWORD (WINAPI *PSS_CAPTURE)(HANDLE, PSS_CAPTURE_FLAGS, DWORD, HPSS *);
typedef DWORD (WINAPI *PSS_FREE)(HANDLE, HPSS);
typedef DWORD (WINAPI *PSS_QUERY)(HPSS, PSS_QUERY_INFORMATION_CLASS, void *, DWORD);
typedef DWORD (WINAPI *PSS_WALK)(HPSS, PSS_WALK_INFORMATION_CLASS, HPSSWALK, void *, DWORD);
typedef DWORD (WINAPI *PSS_DUPLICATE)(HANDLE, HPSS, HANDLE, HPSS *, PSS_DUPLICATE_FLAGS);
typedef DWORD (WINAPI *PSS_MARKER_CREATE)(PSS_ALLOCATOR const *, HPSSWALK *);
typedef DWORD (WINAPI *PSS_MARKER)(HPSSWALK);
typedef DWORD (WINAPI *PSS_MARKER_GET)(HPSSWALK, ULONG_PTR *);
typedef DWORD (WINAPI *PSS_MARKER_SET)(HPSSWALK, ULONG_PTR);

static PSS_CAPTURE pCapture;
static PSS_FREE pFree;
static PSS_QUERY pQuery;
static PSS_WALK pWalk;
static PSS_DUPLICATE pDuplicate;
static PSS_MARKER_CREATE pMarkerCreate;
static PSS_MARKER pMarkerFree;
static PSS_MARKER pMarkerSeek;
static PSS_MARKER_GET pMarkerGet;
static PSS_MARKER_SET pMarkerSet;

static LONG Allocations, Frees;

static void *WINAPI CountingAlloc(void *Context, DWORD Size)
{
    ok(Context == &Allocations, "Allocator context %p\n", Context);
    Allocations++;
    return HeapAlloc(GetProcessHeap(), 0, Size);
}

static void WINAPI CountingFree(void *Context, void *Address)
{
    ok(Context == &Allocations, "Allocator context %p\n", Context);
    Frees++;
    HeapFree(GetProcessHeap(), 0, Address);
}

static HPSS Capture(PSS_CAPTURE_FLAGS Flags)
{
    HPSS Snapshot = NULL;
    DWORD Error = pCapture(GetCurrentProcess(), Flags, CONTEXT_ALL, &Snapshot);

    ok(Error == ERROR_SUCCESS, "Capture %#x returned %lu\n", Flags, Error);
    return Error == ERROR_SUCCESS ? Snapshot : NULL;
}

static HPSSWALK NewMarker(void)
{
    HPSSWALK Marker = NULL;
    DWORD Error = pMarkerCreate(NULL, &Marker);

    ok(Error == ERROR_SUCCESS && Marker, "PssWalkMarkerCreate returned %lu\n", Error);
    return Marker;
}

static void TestThreads(void)
{
    PSS_THREAD_INFORMATION Info;
    PSS_THREAD_ENTRY Entry;
    HPSS Snapshot = Capture(PSS_CAPTURE_THREADS | PSS_CAPTURE_THREAD_CONTEXT);
    HPSSWALK Marker = NewMarker();
    ULONG_PTR Position, Saved = 0;
    DWORD Error, Count = 0, SavedId = 0;
    BOOL Found = FALSE;

    if (!Snapshot || !Marker)
        return;
    Error = pQuery(Snapshot, PSS_QUERY_THREAD_INFORMATION, &Info, sizeof(Info));
    ok(Error == ERROR_SUCCESS, "Thread query returned %lu\n", Error);
    ok(Info.ThreadsCaptured >= 1, "%lu threads captured\n", Info.ThreadsCaptured);
    ok(Info.ContextLength == sizeof(CONTEXT), "Context length %lu\n", Info.ContextLength);
    ok(pMarkerGet(Marker, &Position) == ERROR_SUCCESS && Position == 0, "Initial position %Iu\n", (size_t)Position);

    while ((Error = pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry))) == ERROR_SUCCESS)
    {
        Count++;
        ok(Entry.ProcessId == GetCurrentProcessId(), "Thread %lu of process %lu\n", Entry.ThreadId, Entry.ProcessId);
        if (Entry.ThreadId == GetCurrentThreadId())
        {
            Found = TRUE;
            ok(!(Entry.Flags & PSS_THREAD_FLAGS_TERMINATED), "The current thread is marked terminated\n");
            ok(Entry.ExitStatus == STILL_ACTIVE, "Exit status %#lx\n", Entry.ExitStatus);
            ok(Entry.TebBaseAddress == NtCurrentTeb(), "TEB %p instead of %p\n", Entry.TebBaseAddress, NtCurrentTeb());
            ok(Entry.ContextRecord != NULL, "No context for the current thread\n");
            ok(Entry.SizeOfContextRecord == sizeof(CONTEXT), "Context size %u\n", Entry.SizeOfContextRecord);
        }
    }
    ok(Error == ERROR_NO_MORE_ITEMS, "Thread walk ended with %lu\n", Error);
    ok(Found, "The current thread is missing\n");
    ok(Count == Info.ThreadsCaptured, "Walked %lu threads, captured %lu\n", Count, Info.ThreadsCaptured);
    Error = pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry));
    ok(Error == ERROR_NO_MORE_ITEMS, "Walk past the end returned %lu\n", Error);

    ok(pMarkerSeek(Marker) == ERROR_SUCCESS, "Seek failed\n");
    ok(pMarkerGet(Marker, &Saved) == ERROR_SUCCESS, "GetPosition failed\n");
    ok(pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry)) == ERROR_SUCCESS, "Walk after seek failed\n");
    SavedId = Entry.ThreadId;
    ok(pMarkerSet(Marker, Saved) == ERROR_SUCCESS, "SetPosition failed\n");
    ok(pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry)) == ERROR_SUCCESS, "Walk after SetPosition failed\n");
    ok(Entry.ThreadId == SavedId, "Restored position gave thread %lu instead of %lu\n", Entry.ThreadId, SavedId);
    ok(pMarkerSet(Marker, 100000) == ERROR_SUCCESS, "SetPosition past the end failed\n");
    Error = pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry));
    ok(Error == ERROR_NO_MORE_ITEMS, "Walk past a far position returned %lu\n", Error);

    ok(pMarkerSeek(Marker) == ERROR_SUCCESS, "Seek failed\n");
    Error = pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry) - 1);
    ok(Error == ERROR_BAD_LENGTH, "Short walk buffer returned %lu\n", Error);
    Error = pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry) + 8);
    ok(Error == ERROR_BAD_LENGTH, "Long walk buffer returned %lu\n", Error);
    Error = pWalk(Snapshot, (PSS_WALK_INFORMATION_CLASS)9, Marker, &Entry, sizeof(Entry));
    ok(Error == ERROR_INVALID_PARAMETER, "Unknown walk class returned %lu\n", Error);
    Error = pWalk(Snapshot, PSS_WALK_HANDLES, Marker, &Entry, sizeof(PSS_HANDLE_ENTRY));
    ok(Error == ERROR_NOT_FOUND, "Uncaptured handle walk returned %lu\n", Error);
    Error = pWalk(Snapshot, PSS_WALK_AUXILIARY_PAGES, Marker, &Entry, sizeof(PSS_AUXILIARY_PAGE_ENTRY));
    ok(Error == ERROR_NOT_FOUND, "Uncaptured auxiliary page walk returned %lu\n", Error);
    Error = pWalk(Snapshot, PSS_WALK_HANDLES, Marker, &Entry, 4);
    ok(Error == ERROR_BAD_LENGTH, "Uncaptured walk with a bad length returned %lu\n", Error);
    Error = pWalk(NULL, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry));
    ok(Error == ERROR_INVALID_HANDLE, "Null snapshot walk returned %lu\n", Error);

    ok(pMarkerFree(Marker) == ERROR_SUCCESS, "Marker free failed\n");
    ok(pFree(GetCurrentProcess(), Snapshot) == ERROR_SUCCESS, "Snapshot free failed\n");

    Snapshot = Capture(PSS_CAPTURE_THREADS);
    if (Snapshot)
    {
        Error = pQuery(Snapshot, PSS_QUERY_THREAD_INFORMATION, &Info, sizeof(Info));
        ok(Error == ERROR_SUCCESS && Info.ThreadsCaptured >= 1, "Thread query without contexts returned %lu\n", Error);
        ok(Info.ContextLength == 0, "Context length without contexts %lu\n", Info.ContextLength);
        pFree(GetCurrentProcess(), Snapshot);
    }
}

static void TestHandles(void)
{
    static const WCHAR EventName[] = L"LiberNTPssWalkEvent";
    HANDLE Event = CreateEventW(NULL, TRUE, TRUE, EventName);
    HANDLE Semaphore = CreateSemaphoreW(NULL, 2, 5, NULL);
    PSS_HANDLE_INFORMATION Info;
    PSS_HANDLE_ENTRY Entry;
    HPSS Snapshot;
    HPSSWALK Marker;
    DWORD Error, Count = 0;
    BOOL FoundEvent = FALSE, FoundSemaphore = FALSE;

    ok(Event && Semaphore, "Object creation failed\n");
    Snapshot = Capture(PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_NAME_INFORMATION |
                       PSS_CAPTURE_HANDLE_BASIC_INFORMATION | PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION);
    Marker = NewMarker();
    if (Snapshot && Marker)
    {
        Error = pQuery(Snapshot, PSS_QUERY_HANDLE_INFORMATION, &Info, sizeof(Info));
        ok(Error == ERROR_SUCCESS && Info.HandlesCaptured >= 2, "Handle query returned %lu, %lu handles\n", Error, Info.HandlesCaptured);
        while ((Error = pWalk(Snapshot, PSS_WALK_HANDLES, Marker, &Entry, sizeof(Entry))) == ERROR_SUCCESS)
        {
            Count++;
            if (Entry.Handle == Event)
            {
                SIZE_T NameChars = Entry.ObjectNameLength / sizeof(WCHAR);

                FoundEvent = TRUE;
                ok(Entry.Flags == (PSS_HANDLE_HAVE_TYPE | PSS_HANDLE_HAVE_NAME | PSS_HANDLE_HAVE_BASIC_INFORMATION |
                                   PSS_HANDLE_HAVE_TYPE_SPECIFIC_INFORMATION), "Event flags %#x\n", Entry.Flags);
                ok(Entry.ObjectType == PSS_OBJECT_TYPE_EVENT, "Event object type %d\n", Entry.ObjectType);
                ok(Entry.TypeNameLength == sizeof(L"Event") && Entry.TypeName && !wcscmp(Entry.TypeName, L"Event"),
                   "Event type name length %u\n", Entry.TypeNameLength);
                ok(Entry.ObjectName && Entry.ObjectNameLength == (wcslen(Entry.ObjectName) + 1) * sizeof(WCHAR),
                   "Event name length %u\n", Entry.ObjectNameLength);
                ok(Entry.ObjectName && NameChars > ARRAYSIZE(EventName) &&
                   !wcscmp(Entry.ObjectName + NameChars - ARRAYSIZE(EventName), EventName),
                   "Event name '%ls'\n", Entry.ObjectName ? Entry.ObjectName : L"");
                ok(Entry.GrantedAccess == EVENT_ALL_ACCESS, "Event access %#lx\n", Entry.GrantedAccess);
                ok(Entry.HandleCount >= 1, "Event handle count %lu\n", Entry.HandleCount);
                ok(Entry.TypeSpecificInformation.Event.ManualReset, "The event is not manual reset\n");
                ok(Entry.TypeSpecificInformation.Event.Signaled, "The event is not signaled\n");
            }
            else if (Entry.Handle == Semaphore)
            {
                FoundSemaphore = TRUE;
                ok(Entry.ObjectType == PSS_OBJECT_TYPE_SEMAPHORE, "Semaphore object type %d\n", Entry.ObjectType);
                ok(Entry.TypeSpecificInformation.Semaphore.CurrentCount == 2, "Semaphore count %ld\n", Entry.TypeSpecificInformation.Semaphore.CurrentCount);
                ok(Entry.TypeSpecificInformation.Semaphore.MaximumCount == 5, "Semaphore maximum %ld\n", Entry.TypeSpecificInformation.Semaphore.MaximumCount);
            }
        }
        ok(Error == ERROR_NO_MORE_ITEMS, "Handle walk ended with %lu\n", Error);
        ok(Count == Info.HandlesCaptured, "Walked %lu handles, captured %lu\n", Count, Info.HandlesCaptured);
        ok(FoundEvent && FoundSemaphore, "Event %d semaphore %d\n", FoundEvent, FoundSemaphore);
    }
    if (Marker) pMarkerFree(Marker);
    if (Snapshot) pFree(GetCurrentProcess(), Snapshot);

    Snapshot = Capture(PSS_CAPTURE_HANDLES);
    Marker = NewMarker();
    FoundEvent = FALSE;
    while (Snapshot && Marker && pWalk(Snapshot, PSS_WALK_HANDLES, Marker, &Entry, sizeof(Entry)) == ERROR_SUCCESS)
    {
        if (Entry.Handle != Event)
            continue;
        FoundEvent = TRUE;
        ok(Entry.Flags == PSS_HANDLE_HAVE_TYPE, "Handles-only event flags %#x\n", Entry.Flags);
        ok(Entry.ObjectType == PSS_OBJECT_TYPE_UNKNOWN, "Handles-only object type %d\n", Entry.ObjectType);
        ok(Entry.TypeNameLength == sizeof(L"Event"), "Handles-only type name length %u\n", Entry.TypeNameLength);
        ok(Entry.ObjectNameLength == 0 && Entry.ObjectName == NULL, "Handles-only name length %u\n", Entry.ObjectNameLength);
        ok(Entry.GrantedAccess == 0, "Handles-only access %#lx\n", Entry.GrantedAccess);
    }
    ok(FoundEvent, "Handles-only snapshot misses the event\n");
    if (Marker) pMarkerFree(Marker);
    if (Snapshot) pFree(GetCurrentProcess(), Snapshot);

    Snapshot = Capture(PSS_CAPTURE_HANDLE_NAME_INFORMATION);
    Marker = NewMarker();
    if (Snapshot && Marker)
    {
        Error = pWalk(Snapshot, PSS_WALK_HANDLES, Marker, &Entry, sizeof(Entry));
        ok(Error == ERROR_NOT_FOUND, "Name information without handles walked with %lu\n", Error);
    }
    if (Marker) pMarkerFree(Marker);
    if (Snapshot) pFree(GetCurrentProcess(), Snapshot);
    CloseHandle(Event);
    CloseHandle(Semaphore);
}

static void TestVaSpace(void)
{
    PBYTE Module = (PBYTE)GetModuleHandleW(NULL);
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)(Module + ((PIMAGE_DOS_HEADER)Module)->e_lfanew);
    PSS_VA_SPACE_INFORMATION Info;
    PSS_VA_SPACE_ENTRY Entry;
    HPSS Snapshot;
    HPSSWALK Marker;
    DWORD Error, Count = 0, Pass;
    BOOL Found;

    for (Pass = 0; Pass < 2; Pass++)
    {
        Snapshot = Capture(Pass ? PSS_CAPTURE_VA_SPACE : (PSS_CAPTURE_VA_SPACE | PSS_CAPTURE_VA_SPACE_SECTION_INFORMATION));
        Marker = NewMarker();
        Found = FALSE;
        Count = 0;
        if (!Snapshot || !Marker)
            continue;
        Error = pQuery(Snapshot, PSS_QUERY_VA_SPACE_INFORMATION, &Info, sizeof(Info));
        ok(Error == ERROR_SUCCESS && Info.RegionCount > 1, "Address space query returned %lu, %lu regions\n", Error, Info.RegionCount);
        while ((Error = pWalk(Snapshot, PSS_WALK_VA_SPACE, Marker, &Entry, sizeof(Entry))) == ERROR_SUCCESS)
        {
            if (Count++ == 0)
                ok(Entry.BaseAddress == NULL && Entry.State == MEM_FREE, "First region %p state %#lx\n", Entry.BaseAddress, Entry.State);
            if (Entry.BaseAddress != Module)
                continue;
            Found = TRUE;
            ok(Entry.AllocationBase == Module, "Image allocation base %p\n", Entry.AllocationBase);
            ok(Entry.State == MEM_COMMIT && Entry.Type == MEM_IMAGE, "Image state %#lx type %#lx\n", Entry.State, Entry.Type);
            if (!Pass)
            {
                ok(Entry.TimeDateStamp == Nt->FileHeader.TimeDateStamp, "Time stamp %#lx\n", Entry.TimeDateStamp);
                ok(Entry.SizeOfImage == Nt->OptionalHeader.SizeOfImage, "Image size %#lx\n", Entry.SizeOfImage);
                ok(Entry.ImageBase == (PVOID)(ULONG_PTR)Nt->OptionalHeader.ImageBase, "Image base %p\n", Entry.ImageBase);
                ok(Entry.MappedFileName && Entry.MappedFileNameLength == (wcslen(Entry.MappedFileName) + 1) * sizeof(WCHAR),
                   "Mapped file name length %u\n", Entry.MappedFileNameLength);
            }
            else
            {
                ok(Entry.TimeDateStamp == 0 && Entry.SizeOfImage == 0 && Entry.ImageBase == NULL,
                   "Section information without the flag: %#lx %#lx %p\n", Entry.TimeDateStamp, Entry.SizeOfImage, Entry.ImageBase);
                ok(Entry.MappedFileName == NULL && Entry.MappedFileNameLength == 0, "Mapped file name without the flag\n");
            }
        }
        ok(Error == ERROR_NO_MORE_ITEMS, "Address space walk ended with %lu\n", Error);
        ok(Count == Info.RegionCount, "Walked %lu regions, captured %lu\n", Count, Info.RegionCount);
        ok(Found, "The executable image region is missing\n");
        pMarkerFree(Marker);
        pFree(GetCurrentProcess(), Snapshot);
    }
}

static void TestMarkersAndDuplicates(void)
{
    PSS_ALLOCATOR Allocator = { &Allocations, CountingAlloc, CountingFree };
    PSS_PERFORMANCE_COUNTERS Counters;
    PSS_THREAD_INFORMATION Info, DuplicateInfo;
    PSS_THREAD_ENTRY Entry;
    HPSS Snapshot = Capture(PSS_CAPTURE_THREADS), Duplicate = NULL;
    HPSSWALK Marker = NULL;
    ULONG_PTR Position;
    DWORD Error;

    if (!Snapshot)
        return;
    Error = pMarkerCreate(&Allocator, &Marker);
    ok(Error == ERROR_SUCCESS, "Allocator marker returned %lu\n", Error);
    ok(Allocations == 1, "Marker creation allocated %ld times\n", Allocations);
    ok(pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry)) == ERROR_SUCCESS, "Allocator walk failed\n");
    pWalk(Snapshot, PSS_WALK_THREADS, Marker, &Entry, sizeof(Entry));
    ok(Allocations == 1, "Walks allocated through the allocator: %ld\n", Allocations);
    Error = pMarkerFree(Marker);
    ok(Error == ERROR_SUCCESS, "Allocator marker free returned %lu\n", Error);
    ok(Frees == 1, "Marker free released %ld times\n", Frees);

    ok(pMarkerFree(NULL) == ERROR_INVALID_HANDLE, "Null marker free succeeded\n");
    ok(pMarkerGet(NULL, &Position) == ERROR_INVALID_HANDLE, "Null marker position succeeded\n");
    ok(pMarkerSet(NULL, 0) == ERROR_INVALID_HANDLE, "Null marker set succeeded\n");
    ok(pMarkerSeek(NULL) == ERROR_INVALID_HANDLE, "Null marker seek succeeded\n");

    Error = pDuplicate(GetCurrentProcess(), Snapshot, GetCurrentProcess(), &Duplicate, PSS_DUPLICATE_NONE);
    ok(Error == ERROR_SUCCESS && Duplicate && Duplicate != Snapshot, "Duplicate returned %lu, %p\n", Error, Duplicate);
    if (Error == ERROR_SUCCESS)
    {
        ok(pQuery(Snapshot, PSS_QUERY_THREAD_INFORMATION, &Info, sizeof(Info)) == ERROR_SUCCESS, "Source query failed\n");
        ok(pQuery(Duplicate, PSS_QUERY_THREAD_INFORMATION, &DuplicateInfo, sizeof(DuplicateInfo)) == ERROR_SUCCESS, "Duplicate query failed\n");
        ok(Info.ThreadsCaptured == DuplicateInfo.ThreadsCaptured, "Duplicate holds %lu threads, source %lu\n",
           DuplicateInfo.ThreadsCaptured, Info.ThreadsCaptured);
        ok(pFree(GetCurrentProcess(), Duplicate) == ERROR_SUCCESS, "Duplicate free failed\n");
    }
    Duplicate = NULL;
    Error = pDuplicate(GetCurrentProcess(), Snapshot, GetCurrentProcess(), &Duplicate, PSS_DUPLICATE_CLOSE_SOURCE);
    ok(Error == ERROR_SUCCESS && Duplicate, "Close-source duplicate returned %lu\n", Error);
    Error = pQuery(Snapshot, PSS_QUERY_THREAD_INFORMATION, &Info, sizeof(Info));
    ok(Error == ERROR_INVALID_HANDLE, "Closed source query returned %lu\n", Error);

    memset(&Counters, 0xcc, sizeof(Counters));
    Error = pQuery(Duplicate, PSS_QUERY_PERFORMANCE_COUNTERS, &Counters, sizeof(Counters));
    ok(Error == ERROR_SUCCESS, "Performance query returned %lu\n", Error);
    ok(Counters.TotalCycleCount == 0 && Counters.TotalWallClockPeriod == 0, "Unmeasured counters %I64u %I64u\n",
       Counters.TotalCycleCount, Counters.TotalWallClockPeriod);
    Error = pQuery(Duplicate, PSS_QUERY_PERFORMANCE_COUNTERS, &Counters, sizeof(Counters) - 8);
    ok(Error == ERROR_BAD_LENGTH, "Short performance query returned %lu\n", Error);
    if (Duplicate) pFree(GetCurrentProcess(), Duplicate);

    Snapshot = Capture(PSS_CAPTURE_THREADS | PSS_CREATE_MEASURE_PERFORMANCE);
    if (Snapshot)
    {
        Error = pQuery(Snapshot, PSS_QUERY_PERFORMANCE_COUNTERS, &Counters, sizeof(Counters));
        ok(Error == ERROR_SUCCESS, "Measured performance query returned %lu\n", Error);
        ok(Counters.TotalCycleCount != 0 && Counters.ThreadsCycleCount != 0, "Measured cycles %I64u %I64u\n",
           Counters.TotalCycleCount, Counters.ThreadsCycleCount);
        ok(Counters.TotalCycleCount >= Counters.ThreadsCycleCount, "Total cycles below thread cycles\n");
        pFree(GetCurrentProcess(), Snapshot);
    }

    Snapshot = Capture(PSS_CAPTURE_HANDLE_TRACE | PSS_CAPTURE_IPT_TRACE);
    if (Snapshot)
    {
        PSS_HANDLE_TRACE_INFORMATION Trace;
        Error = pQuery(Snapshot, PSS_QUERY_HANDLE_TRACE_INFORMATION, &Trace, sizeof(Trace));
        ok(Error == ERROR_NOT_FOUND, "Handle trace query returned %lu\n", Error);
        pFree(GetCurrentProcess(), Snapshot);
    }
}

START_TEST(PssWalk)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pCapture = (PSS_CAPTURE)GetProcAddress(Kernel32, "PssCaptureSnapshot");
    pFree = (PSS_FREE)GetProcAddress(Kernel32, "PssFreeSnapshot");
    pQuery = (PSS_QUERY)GetProcAddress(Kernel32, "PssQuerySnapshot");
    pWalk = (PSS_WALK)GetProcAddress(Kernel32, "PssWalkSnapshot");
    pDuplicate = (PSS_DUPLICATE)GetProcAddress(Kernel32, "PssDuplicateSnapshot");
    pMarkerCreate = (PSS_MARKER_CREATE)GetProcAddress(Kernel32, "PssWalkMarkerCreate");
    pMarkerFree = (PSS_MARKER)GetProcAddress(Kernel32, "PssWalkMarkerFree");
    pMarkerSeek = (PSS_MARKER)GetProcAddress(Kernel32, "PssWalkMarkerSeekToBeginning");
    pMarkerGet = (PSS_MARKER_GET)GetProcAddress(Kernel32, "PssWalkMarkerGetPosition");
    pMarkerSet = (PSS_MARKER_SET)GetProcAddress(Kernel32, "PssWalkMarkerSetPosition");
    if (!pCapture || !pFree || !pQuery || !pWalk || !pDuplicate || !pMarkerCreate || !pMarkerFree ||
        !pMarkerSeek || !pMarkerGet || !pMarkerSet)
    {
        ok(0, "Process snapshot walk functions are missing\n");
        return;
    }
    TestThreads();
    TestHandles();
    TestVaSpace();
    TestMarkersAndDuplicates();
}
