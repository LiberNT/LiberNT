/*
 * PROJECT:     LiberNT diskbench
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unbuffered disk read/write benchmark
 * COPYRIGHT:   Copyright 2026 LiberNT Project
 *
 * Usage: diskbench                  benchmark C: and print a table
 *        diskbench <seconds> [drive] load a volume (C: by default) for that long and measure responsiveness
 *        diskbench <file-on-target-volume> [file-size-MB]
 *
 * The file is created with FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
 * so every transfer goes through the storage stack to the device; the cache
 * manager never serves or absorbs any of it. Results print as one line per
 * phase: DISKBENCH <phase> <block-size> <MB/s> <IOPS>.
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reactos/storage_read_benchmark.h>

#define DEFAULT_FILE_MB 64
#define RANDOM_OPS      2048

static ULONG RandomOps = RANDOM_OPS;

#define MAX_RESULTS     16
#define STRESS_WRITERS  3
#define STRESS_FILE_MB  64
#define STRESS_PROBE_MS 100

typedef struct _PHASE_RESULT
{
    const char* Phase;
    ULONG BlockSize;
    double MBs;
    double Iops;
} PHASE_RESULT;

static PHASE_RESULT Results[MAX_RESULTS];
static ULONG ResultCount;

static LARGE_INTEGER Frequency;

static double ElapsedSeconds(LARGE_INTEGER Start, LARGE_INTEGER End)
{
    return (double)(End.QuadPart - Start.QuadPart) / (double)Frequency.QuadPart;
}

static ULONG NextRandom(PULONG Seed)
{
    *Seed = *Seed * 1103515245 + 12345;
    return (*Seed >> 8) & 0x7FFFFF;
}

static void Report(const char* Format, ...)
{
    char Line[192];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Line, sizeof(Line) - 1, Format, Args);
    va_end(Args);
    Line[sizeof(Line) - 1] = 0;
    /* Both channels: stdout for a console, the debugger for serial logs. */
    printf("%s", Line);
    fflush(stdout);
    OutputDebugStringA(Line);
}

static void ReportPhase(const char* Phase, ULONG BlockSize, ULONGLONG Bytes, ULONG Ops, double Seconds, DWORD TickMs)
{
    double MBs = Seconds > 0.0 ? ((double)Bytes / (1024.0 * 1024.0)) / Seconds : 0.0;
    double Iops = Seconds > 0.0 ? (double)Ops / Seconds : 0.0;

    Report("DISKBENCH %s %lu %.2f %.0f ticks %lu\n", Phase, BlockSize, MBs, Iops, TickMs);
    if (ResultCount < MAX_RESULTS)
    {
        Results[ResultCount].Phase = Phase;
        Results[ResultCount].BlockSize = BlockSize;
        Results[ResultCount].MBs = MBs;
        Results[ResultCount].Iops = Iops;
        ResultCount++;
    }
}

static void ReportBlock(ULONG BlockSize, char* Text, size_t Size)
{
    if (BlockSize >= 1024 * 1024)
        _snprintf(Text, Size, "%lu MB", BlockSize >> 20);
    else
        _snprintf(Text, Size, "%lu KB", BlockSize >> 10);
    Text[Size - 1] = 0;
}

static void ReportTable(const char* Target, ULONGLONG FileSize)
{
    ULONG Index;

    Report("\ndiskbench %s %I64u MB file, unbuffered, write-through\n", Target, FileSize >> 20);
    Report("  %-10s %8s %10s %10s\n", "Test", "Block", "MB/s", "IOPS");
    for (Index = 0; Index < ResultCount; Index++)
    {
        char Block[16];

        ReportBlock(Results[Index].BlockSize, Block, sizeof(Block));
        Report("  %-10s %8s %10.2f %10.0f\n", Results[Index].Phase, Block, Results[Index].MBs, Results[Index].Iops);
    }
}

static BOOL RunSequential(HANDLE File, BOOL Write, ULONG BlockSize, ULONGLONG FileSize, PUCHAR Buffer, const char* Phase)
{
    LARGE_INTEGER Start, End, Offset;
    ULONGLONG Done = 0;
    ULONG Ops = 0;
    DWORD Transferred;
    DWORD Tick0 = GetTickCount();

    Offset.QuadPart = 0;
    if (SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) == FALSE)
        return FALSE;

    QueryPerformanceCounter(&Start);
    while (Done + BlockSize <= FileSize)
    {
        BOOL Ok = Write ? WriteFile(File, Buffer, BlockSize, &Transferred, NULL)
                        : ReadFile(File, Buffer, BlockSize, &Transferred, NULL);

        if (!Ok || Transferred != BlockSize)
        {
            Report("DISKBENCH %s FAILED error %lu at %I64u\n", Phase, GetLastError(), Done);
            return FALSE;
        }
        Done += BlockSize;
        Ops++;
    }
    QueryPerformanceCounter(&End);

    ReportPhase(Phase, BlockSize, Done, Ops, ElapsedSeconds(Start, End), GetTickCount() - Tick0);
    return TRUE;
}

static BOOL RunRandom(HANDLE File, BOOL Write, ULONG BlockSize, ULONGLONG FileSize, PUCHAR Buffer, const char* Phase)
{
    LARGE_INTEGER Start, End, Offset;
    ULONGLONG Blocks = FileSize / BlockSize;
    ULONG Seed = 0x5EED5EED;
    ULONG Ops;
    DWORD Transferred;
    DWORD Tick0 = GetTickCount();

    QueryPerformanceCounter(&Start);
    for (Ops = 0; Ops < RandomOps; Ops++)
    {
        BOOL Ok;

        Offset.QuadPart = (LONGLONG)(NextRandom(&Seed) % Blocks) * BlockSize;
        if (SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) == FALSE)
            return FALSE;

        Ok = Write ? WriteFile(File, Buffer, BlockSize, &Transferred, NULL)
                   : ReadFile(File, Buffer, BlockSize, &Transferred, NULL);
        if (!Ok || Transferred != BlockSize)
        {
            Report("DISKBENCH %s FAILED error %lu at %I64d\n", Phase, GetLastError(), Offset.QuadPart);
            return FALSE;
        }
    }
    QueryPerformanceCounter(&End);

    ReportPhase(Phase, BlockSize, (ULONGLONG)Ops * BlockSize, Ops, ElapsedSeconds(Start, End), GetTickCount() - Tick0);
    return TRUE;
}

/* Overlapped read-only bench: keeps Depth requests in flight against the
 * raw disk so the queue actually fills; the synchronous scan above only
 * measures per-request latency. */
static void AsyncRawPhase(HANDLE Disk, ULONG DiskNumber, ULONGLONG Limit,
                          ULONG BlockSize, ULONG Depth, BOOL Random,
                          ULONG MaxOps, ULONG MaxMs)
{
    OVERLAPPED Slots[64];
    PUCHAR Region;
    ULONGLONG NextOffset = 0;
    ULONGLONG Bytes = 0;
    ULONG Seed = 0x5EED5EED;
    ULONG Issued = 0;
    ULONG Done = 0;
    ULONG Index;
    DWORD Tick0;
    DWORD Elapsed;
    char Tag[32];
    BOOL Failed = FALSE;

    if (Depth > 64)
        Depth = 64;
    Region = (PUCHAR)VirtualAlloc(NULL, (SIZE_T)Depth * BlockSize, MEM_COMMIT, PAGE_READWRITE);
    if (!Region)
        return;
    memset(Slots, 0, sizeof(Slots));
    for (Index = 0; Index < Depth; Index++)
    {
        Slots[Index].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!Slots[Index].hEvent)
        {
            while (Index)
                CloseHandle(Slots[--Index].hEvent);
            VirtualFree(Region, 0, MEM_RELEASE);
            return;
        }
    }

    Tick0 = GetTickCount();
    while (Done < MaxOps && !Failed)
    {
        Index = Done % Depth;
        if (Issued >= Depth)
        {
            DWORD Transferred;

            if (!GetOverlappedResult(Disk, &Slots[Index], &Transferred, TRUE) ||
                Transferred != BlockSize)
            {
                Report("DISKBENCH ARAW %lu FAILED error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
                break;
            }
            Bytes += Transferred;
            Done++;
        }
        if (Issued < MaxOps && (GetTickCount() - Tick0) < MaxMs)
        {
            ULONGLONG Offset;
            DWORD Transferred;

            if (Random)
                Offset = (ULONGLONG)(NextRandom(&Seed) % (ULONG)(Limit >> 12)) << 12;
            else
            {
                Offset = NextOffset;
                NextOffset += BlockSize;
                if (NextOffset + BlockSize > Limit)
                    NextOffset = 0;
            }
            Index = Issued % Depth;
            ResetEvent(Slots[Index].hEvent);
            Slots[Index].Offset = (DWORD)Offset;
            Slots[Index].OffsetHigh = (DWORD)(Offset >> 32);
            if (!ReadFile(Disk, Region + (SIZE_T)Index * BlockSize, BlockSize, &Transferred, &Slots[Index]) &&
                GetLastError() != ERROR_IO_PENDING)
            {
                Report("DISKBENCH ARAW %lu FAILED submit error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
                break;
            }
            Issued++;
        }
        else if (Issued == Done)
        {
            break;
        }
    }
    while (Done < Issued)
    {
        DWORD Transferred;

        Index = Done % Depth;
        if (GetOverlappedResult(Disk, &Slots[Index], &Transferred, TRUE))
            Bytes += Transferred;
        Done++;
    }
    Elapsed = GetTickCount() - Tick0;

    if (!Failed)
    {
        _snprintf(Tag, sizeof(Tag), "ARAW%s-%lu QD%lu", Random ? "RND" : "SEQ", DiskNumber, Depth);
        Report("DISKBENCH %s %lu ops %lu MB %I64u ticks %lu\n",
               Tag, BlockSize, Done, Bytes >> 20, Elapsed);
    }
    for (Index = 0; Index < Depth; Index++)
        CloseHandle(Slots[Index].hEvent);
    VirtualFree(Region, 0, MEM_RELEASE);
}

static int AsyncRawScan(ULONG Depth)
{
    char Path[32];
    ULONG DiskNumber;

    for (DiskNumber = 0; DiskNumber < 8; DiskNumber++)
    {
        HANDLE Disk;
        GET_LENGTH_INFORMATION LengthInfo;
        DWORD Transferred;
        ULONGLONG Limit;

        _snprintf(Path, sizeof(Path), "\\\\.\\PhysicalDrive%lu", DiskNumber);
        Disk = CreateFileA(Path,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
                           NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;
        if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                             &LengthInfo, sizeof(LengthInfo), &Transferred, NULL))
            LengthInfo.Length.QuadPart = 0;
        Limit = (ULONGLONG)LengthInfo.Length.QuadPart;
        Report("DISKBENCH ARAWDISK %lu size %I64u MB\n", DiskNumber, Limit >> 20);
        if (Limit < 64 * 1024 * 1024)
        {
            CloseHandle(Disk);
            continue;
        }
        if (Limit > 1024ULL * 1024 * 1024)
            Limit = 1024ULL * 1024 * 1024;

        /* CrystalDiskMark-style SEQ1M Q8, then deeper small-block phases. */
        AsyncRawPhase(Disk, DiskNumber, Limit, 1024 * 1024, 8, FALSE, 4096, 8000);
        AsyncRawPhase(Disk, DiskNumber, Limit, 128 * 1024, Depth, FALSE, 4096, 8000);
        AsyncRawPhase(Disk, DiskNumber, Limit, 4096, Depth, TRUE, 16384, 8000);
        CloseHandle(Disk);
    }
    Report("DISKBENCH ARAWSCAN DONE\n");
    return 0;
}

/* Read-only bench straight off the disk devices: identifies every disk by
 * size and measures the port driver path with no filesystem in between. */
static int RawScan(PUCHAR Buffer)
{
    char Path[32];
    ULONG DiskNumber;

    for (DiskNumber = 0; DiskNumber < 8; DiskNumber++)
    {
        HANDLE Disk;
        GET_LENGTH_INFORMATION LengthInfo;
        LARGE_INTEGER Start, End, Offset;
        ULONGLONG Limit;
        ULONG Seed = 0x5EED5EED;
        ULONG Ops;
        DWORD Transferred;
        DWORD Tick0;
        BOOL Failed = FALSE;

        _snprintf(Path, sizeof(Path), "\\\\.\\PhysicalDrive%lu", DiskNumber);
        Disk = CreateFileA(Path,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_NO_BUFFERING,
                           NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;

        if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                             &LengthInfo, sizeof(LengthInfo), &Transferred, NULL))
            LengthInfo.Length.QuadPart = 0;
        Report("DISKBENCH RAWDISK %lu size %I64u MB\n",
               DiskNumber, (ULONGLONG)LengthInfo.Length.QuadPart >> 20);

        Limit = (ULONGLONG)LengthInfo.Length.QuadPart;
        if (Limit < 32 * 1024 * 1024)
        {
            CloseHandle(Disk);
            continue;
        }

        Offset.QuadPart = 0;
        SetFilePointerEx(Disk, Offset, NULL, FILE_BEGIN);
        Tick0 = GetTickCount();
        QueryPerformanceCounter(&Start);
        for (Ops = 0; Ops < 16; Ops++)
        {
            if (!ReadFile(Disk, Buffer, 1024 * 1024, &Transferred, NULL) || Transferred != 1024 * 1024)
            {
                Report("DISKBENCH RAWSEQ %lu FAILED error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
                break;
            }
        }
        QueryPerformanceCounter(&End);
        if (!Failed)
        {
            char Tag[24];

            _snprintf(Tag, sizeof(Tag), "RAWSEQ-%lu", DiskNumber);
            ReportPhase(Tag, 1024 * 1024, (ULONGLONG)Ops << 20, Ops,
                        ElapsedSeconds(Start, End), GetTickCount() - Tick0);
        }

        if (Limit > 1024ULL * 1024 * 1024)
            Limit = 1024ULL * 1024 * 1024;
        Tick0 = GetTickCount();
        QueryPerformanceCounter(&Start);
        for (Ops = 0; Ops < 256 && !Failed; Ops++)
        {
            Offset.QuadPart = (LONGLONG)(NextRandom(&Seed) % (ULONG)(Limit >> 12)) << 12;
            SetFilePointerEx(Disk, Offset, NULL, FILE_BEGIN);
            if (!ReadFile(Disk, Buffer, 4096, &Transferred, NULL) || Transferred != 4096)
            {
                Report("DISKBENCH RAWRND %lu FAILED error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
            }
        }
        QueryPerformanceCounter(&End);
        if (!Failed)
        {
            char Tag[24];

            _snprintf(Tag, sizeof(Tag), "RAWRND-%lu", DiskNumber);
            ReportPhase(Tag, 4096, (ULONGLONG)Ops << 12, Ops,
                        ElapsedSeconds(Start, End), GetTickCount() - Tick0);
        }
        CloseHandle(Disk);
    }
    Report("DISKBENCH RAWSCAN DONE\n");
    return 0;
}

static int RawReadBenchmark(int argc, char* argv[], PUCHAR Buffer)
{
    char Path[32];
    HANDLE Disk;
    GET_LENGTH_INFORMATION LengthInfo;
    LARGE_INTEGER Start, End, Offset;
    LARGE_INTEGER IoStart, IoEnd;
    ULONGLONG StartOffset;
    ULONGLONG Length;
    ULONGLONG Done;
    ULONGLONG MaxIoOffset;
    ULONGLONG MaxIoUs;
    LONGLONG IoTicks;
    LONGLONG MaxIoTicks;
    ULONG DiskNumber;
    ULONG BlockSize;
    ULONG Runs;
    ULONG Run;
    ULONG Ops;
    ULONG Over10Ms;
    ULONG Over100Ms;
    ULONG Over1S;
    DWORD Transferred;
    DWORD Tick0;
    DWORD ReadError;
    BOOL ReadOk;
    BOOL Failed = FALSE;

    DiskNumber = argc >= 3 ? strtoul(argv[2], NULL, 0) : 0;
    StartOffset = (ULONGLONG)(argc >= 4 ? strtoul(argv[3], NULL, 0) :
                              (ULONG)(STORAGE_READ_BENCHMARK_OFFSET >> 20)) << 20;
    Length = (ULONGLONG)(argc >= 5 ? strtoul(argv[4], NULL, 0) :
                         (ULONG)(STORAGE_READ_BENCHMARK_LENGTH >> 20)) << 20;
    BlockSize = (argc >= 6 ? strtoul(argv[5], NULL, 0) :
                 STORAGE_READ_BENCHMARK_BLOCK_SIZE >> 10) << 10;
    Runs = argc >= 7 ? strtoul(argv[6], NULL, 0) : STORAGE_READ_BENCHMARK_USER_RUNS;

    if (DiskNumber >= 32 ||
        Length == 0 ||
        BlockSize < 4096 ||
        BlockSize > 1024 * 1024 ||
        (BlockSize & 511) != 0 ||
        (StartOffset % BlockSize) != 0 ||
        (Length % BlockSize) != 0 ||
        Runs == 0 ||
        Runs > 100)
    {
        Report("DISKBENCH RAWREAD SETUP FAILED invalid arguments\n");
        return 1;
    }

    _snprintf(Path, sizeof(Path), "\\\\.\\PhysicalDrive%lu", DiskNumber);
    Disk = CreateFileA(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (Disk == INVALID_HANDLE_VALUE)
    {
        Report("DISKBENCH RAWREAD SETUP FAILED open disk %lu error %lu\n",
               DiskNumber, GetLastError());
        return 1;
    }

    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                         &LengthInfo, sizeof(LengthInfo), &Transferred, NULL))
    {
        Report("DISKBENCH RAWREAD SETUP FAILED length error %lu\n", GetLastError());
        CloseHandle(Disk);
        return 1;
    }
    if (StartOffset > (ULONGLONG)LengthInfo.Length.QuadPart ||
        Length > (ULONGLONG)LengthInfo.Length.QuadPart - StartOffset)
    {
        Report("DISKBENCH RAWREAD SETUP FAILED range disk-bytes %I64u\n",
               (ULONGLONG)LengthInfo.Length.QuadPart);
        CloseHandle(Disk);
        return 1;
    }

    Report("DISKBENCH RAWREAD PROTOCOL disk %lu offset %I64u bytes %I64u block %lu qd 1 runs %lu\n",
           DiskNumber, StartOffset, Length, BlockSize, Runs);
    for (Run = 1; Run <= Runs; Run++)
    {
        char Tag[40];

        Offset.QuadPart = StartOffset;
        if (!SetFilePointerEx(Disk, Offset, NULL, FILE_BEGIN))
        {
            Report("DISKBENCH RAWREAD-%lu-RUN%lu FAILED seek error %lu\n",
                   DiskNumber, Run, GetLastError());
            Failed = TRUE;
            break;
        }

        Done = 0;
        Ops = 0;
        MaxIoOffset = 0;
        MaxIoTicks = 0;
        Over10Ms = 0;
        Over100Ms = 0;
        Over1S = 0;
        Tick0 = GetTickCount();
        QueryPerformanceCounter(&Start);
        while (Done < Length)
        {
            Transferred = 0;
            QueryPerformanceCounter(&IoStart);
            ReadOk = ReadFile(Disk, Buffer, BlockSize, &Transferred, NULL);
            ReadError = ReadOk ? ERROR_SUCCESS : GetLastError();
            QueryPerformanceCounter(&IoEnd);
            IoTicks = IoEnd.QuadPart - IoStart.QuadPart;
            if (IoTicks > MaxIoTicks)
            {
                MaxIoTicks = IoTicks;
                MaxIoOffset = StartOffset + Done;
            }
            if (IoTicks >= Frequency.QuadPart / 100)
                Over10Ms++;
            if (IoTicks >= Frequency.QuadPart / 10)
                Over100Ms++;
            if (IoTicks >= Frequency.QuadPart)
                Over1S++;
            if (!ReadOk || Transferred != BlockSize)
            {
                Report("DISKBENCH RAWREAD-%lu-RUN%lu FAILED error %lu at %I64u transferred %lu\n",
                       DiskNumber, Run, ReadError, StartOffset + Done, Transferred);
                Failed = TRUE;
                break;
            }
            Done += BlockSize;
            Ops++;
        }
        QueryPerformanceCounter(&End);
        if (Failed)
            break;

        _snprintf(Tag, sizeof(Tag), "RAWREAD-%lu-RUN%lu", DiskNumber, Run);
        ReportPhase(Tag, BlockSize, Done, Ops,
                    ElapsedSeconds(Start, End), GetTickCount() - Tick0);
        MaxIoUs = ((ULONGLONG)MaxIoTicks * 1000000) /
                  (ULONGLONG)Frequency.QuadPart;
        Report("DISKBENCH %s LATENCY max-us %I64u offset %I64u over-10ms %lu over-100ms %lu over-1s %lu\n",
               Tag, MaxIoUs, MaxIoOffset, Over10Ms, Over100Ms, Over1S);
    }

    CloseHandle(Disk);
    Report("DISKBENCH RAWREAD DONE status %s\n", Failed ? "failed" : "passed");
    return Failed ? 1 : 0;
}

typedef struct _STRESS_STATE
{
    volatile LONG Stop;
    volatile LONG64 BytesWritten;
    volatile LONG64 RandomOps;
    volatile LONG Errors;
    char Path[STRESS_WRITERS + 1][MAX_PATH];
} STRESS_STATE;

typedef struct _STRESS_WORKER
{
    STRESS_STATE* State;
    ULONG Index;
} STRESS_WORKER;

static DWORD WINAPI StressWriter(LPVOID Context)
{
    STRESS_WORKER* Worker = (STRESS_WORKER*)Context;
    STRESS_STATE* State = Worker->State;
    PUCHAR Block = (PUCHAR)VirtualAlloc(NULL, 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
    HANDLE File;
    ULONGLONG Written = 0;
    DWORD Transferred;

    if (!Block)
    {
        InterlockedIncrement(&State->Errors);
        return 1;
    }
    memset(Block, 0x5A + (int)Worker->Index, 1024 * 1024);
    File = CreateFileA(State->Path[Worker->Index], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        InterlockedIncrement(&State->Errors);
        VirtualFree(Block, 0, MEM_RELEASE);
        return 1;
    }
    while (!State->Stop)
    {
        if (!WriteFile(File, Block, 1024 * 1024, &Transferred, NULL) || Transferred != 1024 * 1024)
        {
            InterlockedIncrement(&State->Errors);
            break;
        }
        InterlockedExchangeAdd64(&State->BytesWritten, Transferred);
        Written += Transferred;
        if ((Written & (16 * 1024 * 1024 - 1)) == 0)
            FlushFileBuffers(File);
        if (Written >= (ULONGLONG)STRESS_FILE_MB * 1024 * 1024)
        {
            LARGE_INTEGER Zero;

            Zero.QuadPart = 0;
            SetFilePointerEx(File, Zero, NULL, FILE_BEGIN);
            Written = 0;
        }
    }
    CloseHandle(File);
    VirtualFree(Block, 0, MEM_RELEASE);
    return 0;
}

static DWORD WINAPI StressRandom(LPVOID Context)
{
    STRESS_STATE* State = (STRESS_STATE*)Context;
    PUCHAR Block = (PUCHAR)VirtualAlloc(NULL, 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
    ULONGLONG FileSize = (ULONGLONG)STRESS_FILE_MB * 1024 * 1024;
    ULONG Seed = 0x0DD5EED5;
    HANDLE File;
    ULONGLONG Done;
    DWORD Transferred;

    if (!Block)
    {
        InterlockedIncrement(&State->Errors);
        return 1;
    }
    File = CreateFileA(State->Path[STRESS_WRITERS], GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        InterlockedIncrement(&State->Errors);
        VirtualFree(Block, 0, MEM_RELEASE);
        return 1;
    }
    for (Done = 0; Done < FileSize && !State->Stop; Done += 1024 * 1024)
    {
        if (!WriteFile(File, Block, 1024 * 1024, &Transferred, NULL))
        {
            InterlockedIncrement(&State->Errors);
            break;
        }
    }
    while (!State->Stop)
    {
        LARGE_INTEGER Offset;
        ULONG Op = NextRandom(&Seed);
        BOOL Ok;

        Offset.QuadPart = (LONGLONG)(Op % (FileSize / 4096)) * 4096;
        if (!SetFilePointerEx(File, Offset, NULL, FILE_BEGIN))
            break;
        Ok = (Op & 1) ? WriteFile(File, Block, 4096, &Transferred, NULL)
                      : ReadFile(File, Block, 4096, &Transferred, NULL);
        if (!Ok || Transferred != 4096)
        {
            InterlockedIncrement(&State->Errors);
            break;
        }
        InterlockedIncrement64(&State->RandomOps);
    }
    CloseHandle(File);
    VirtualFree(Block, 0, MEM_RELEASE);
    return 0;
}

static int CompareLatency(const void* Left, const void* Right)
{
    double A = *(const double*)Left, B = *(const double*)Right;

    return (A > B) - (A < B);
}

static double Percentile(double* Sorted, ULONG Count, ULONG Percent)
{
    ULONG Index;

    if (Count == 0)
        return 0.0;
    Index = (ULONG)(((ULONGLONG)(Count - 1) * Percent) / 100);
    return Sorted[Index];
}

/* Loads the volume for Seconds and measures how long an unrelated open and a 100 ms wake-up take meanwhile */
static int StressVolume(ULONG Seconds, const char* Root)
{
    static STRESS_STATE State;
    STRESS_WORKER Workers[STRESS_WRITERS];
    HANDLE Threads[STRESS_WRITERS + 1];
    char ProbePath[MAX_PATH];
    double* Opens;
    ULONG OpenCount = 0, Capacity, Index, Second;
    double WorstLate = 0.0;
    LONG64 LastBytes = 0, LastOps = 0;
    LARGE_INTEGER Begin, Now;
    HANDLE Probe;

    if (Seconds > 3600)
        Seconds = 3600;
    Capacity = Seconds * (1000 / STRESS_PROBE_MS) + 16;
    Opens = (double*)calloc(Capacity, sizeof(double));
    if (!Opens)
    {
        Report("DISKBENCH STRESS FAILED alloc\n");
        return 1;
    }
    for (Index = 0; Index <= STRESS_WRITERS; Index++)
        _snprintf(State.Path[Index], MAX_PATH, "%s\\diskbench_stress_%lu.tmp", Root, Index);
    _snprintf(ProbePath, MAX_PATH, "%s\\diskbench_probe.tmp", Root);
    Probe = CreateFileA(ProbePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Probe == INVALID_HANDLE_VALUE)
    {
        Report("DISKBENCH STRESS FAILED probe %lu\n", GetLastError());
        free(Opens);
        return 1;
    }
    CloseHandle(Probe);

    Report("DISKBENCH STRESS %s %lu s, %u cached writers + 1 random 4 KB worker\n", Root, Seconds, STRESS_WRITERS);
    for (Index = 0; Index < STRESS_WRITERS; Index++)
    {
        Workers[Index].State = &State;
        Workers[Index].Index = Index;
        Threads[Index] = CreateThread(NULL, 0, StressWriter, &Workers[Index], 0, NULL);
    }
    Threads[STRESS_WRITERS] = CreateThread(NULL, 0, StressRandom, &State, 0, NULL);

    QueryPerformanceCounter(&Begin);
    for (Second = 1; Second <= Seconds; Second++)
    {
        double SecondWorstOpen = 0.0, SecondWorstLate = 0.0;
        LONG64 Bytes, Ops;
        ULONG Tick;

        for (Tick = 0; Tick < 1000 / STRESS_PROBE_MS; Tick++)
        {
            LARGE_INTEGER Before, After, Due;
            double Late, Open;

            Due.QuadPart = Begin.QuadPart +
                           (LONGLONG)(((Second - 1) * 1000 + (Tick + 1) * STRESS_PROBE_MS) * (double)Frequency.QuadPart / 1000.0);
            QueryPerformanceCounter(&Now);
            if (Now.QuadPart < Due.QuadPart)
                Sleep((DWORD)((Due.QuadPart - Now.QuadPart) * 1000 / Frequency.QuadPart));
            QueryPerformanceCounter(&Before);
            Late = Before.QuadPart > Due.QuadPart ? ElapsedSeconds(Due, Before) * 1000.0 : 0.0;
            Probe = CreateFileA(ProbePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            QueryPerformanceCounter(&After);
            if (Probe != INVALID_HANDLE_VALUE)
                CloseHandle(Probe);
            Open = ElapsedSeconds(Before, After) * 1000000.0;
            if (OpenCount < Capacity)
                Opens[OpenCount++] = Open;
            if (Open > SecondWorstOpen)
                SecondWorstOpen = Open;
            if (Late > SecondWorstLate)
                SecondWorstLate = Late;
        }
        if (SecondWorstLate > WorstLate)
            WorstLate = SecondWorstLate;
        Bytes = State.BytesWritten;
        Ops = State.RandomOps;
        Report("DISKBENCH STRESS t=%lus write %.1f MB/s random %I64d IOPS open max %.0f us late max %.0f ms errors %ld\n",
               Second, (double)(Bytes - LastBytes) / (1024.0 * 1024.0), Ops - LastOps,
               SecondWorstOpen, SecondWorstLate, State.Errors);
        LastBytes = Bytes;
        LastOps = Ops;
    }
    InterlockedExchange(&State.Stop, 1);
    WaitForMultipleObjects(STRESS_WRITERS + 1, Threads, TRUE, 30000);
    QueryPerformanceCounter(&Now);
    for (Index = 0; Index <= STRESS_WRITERS; Index++)
    {
        if (Threads[Index])
            CloseHandle(Threads[Index]);
        DeleteFileA(State.Path[Index]);
    }
    DeleteFileA(ProbePath);

    qsort(Opens, OpenCount, sizeof(double), CompareLatency);
    Report("\ndiskbench %s under load for %lu s\n", Root, Seconds);
    Report("  %-26s %12.1f\n", "Cached write MB/s", (double)State.BytesWritten / (1024.0 * 1024.0) / ElapsedSeconds(Begin, Now));
    Report("  %-26s %12.0f\n", "Random 4 KB IOPS", (double)State.RandomOps / ElapsedSeconds(Begin, Now));
    Report("  %-26s %12.0f\n", "File open p50 (us)", Percentile(Opens, OpenCount, 50));
    Report("  %-26s %12.0f\n", "File open p99 (us)", Percentile(Opens, OpenCount, 99));
    Report("  %-26s %12.0f\n", "File open max (us)", OpenCount ? Opens[OpenCount - 1] : 0.0);
    Report("  %-26s %12.0f\n", "Worst wake-up delay (ms)", WorstLate);
    Report("  %-26s %12ld\n", "I/O errors", State.Errors);
    free(Opens);
    return State.Errors ? 1 : 0;
}

static BOOL IsSeconds(const char* Text)
{
    if (!*Text)
        return FALSE;
    for (; *Text; Text++)
    {
        if (*Text < '0' || *Text > '9')
            return FALSE;
    }
    return TRUE;
}

int main(int argc, char* argv[])
{
    HANDLE File;
    PUCHAR Buffer;
    ULONGLONG FileSize;
    ULONG Index;
    const char* Path = argc >= 2 ? argv[1] : "C:\\diskbench.tmp";

    if (argc >= 2 && (strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "/?") == 0))
    {
        printf("Usage: diskbench                benchmark C: and print a table\n"
               "       diskbench <seconds> [drive] load a volume (C: by default) and measure responsiveness\n"
               "       diskbench <file> [size-MB] [random-ops] | diskbench -raw |\n"
               "       diskbench -rawread [disk] [offset-MB] [size-MB] [block-KB] [runs]\n");
        return 1;
    }
    FileSize = (ULONGLONG)(argc >= 3 ? atoi(argv[2]) : DEFAULT_FILE_MB) * 1024 * 1024;
    if (FileSize == 0)
        FileSize = (ULONGLONG)DEFAULT_FILE_MB * 1024 * 1024;
    if (argc >= 4 && atoi(argv[3]) > 0)
        RandomOps = atoi(argv[3]);

    QueryPerformanceFrequency(&Frequency);

    Buffer = (PUCHAR)VirtualAlloc(NULL, 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
    if (!Buffer)
    {
        Report("DISKBENCH SETUP FAILED alloc\n");
        return 1;
    }
    for (Index = 0; Index < 1024 * 1024; Index++)
        Buffer[Index] = (UCHAR)(Index * 2654435761u >> 24);

    if (argc >= 2 && strcmp(argv[1], "-raw") == 0)
        return RawScan(Buffer);
    if (argc >= 2 && strcmp(argv[1], "-rawread") == 0)
        return RawReadBenchmark(argc, argv, Buffer);
    if (argc >= 2 && strcmp(argv[1], "-araw") == 0)
        return AsyncRawScan(argc >= 3 && atoi(argv[2]) > 0 ? atoi(argv[2]) : 32);
    if ((argc == 2 || argc == 3) && IsSeconds(argv[1]))
        return StressVolume((ULONG)atoi(argv[1]) ? (ULONG)atoi(argv[1]) : 1, argc == 3 ? argv[2] : "C:");

    File = CreateFileA(Path,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        DeleteFileA(Path);
        File = CreateFileA(Path,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           CREATE_ALWAYS,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                           NULL);
    }
    if (File == INVALID_HANDLE_VALUE)
    {
        Report("DISKBENCH SETUP FAILED open %lu\n", GetLastError());
        return 1;
    }

    /* Sequential write fills the file; everything after reuses it. */
    if (!RunSequential(File, TRUE, 1024 * 1024, FileSize, Buffer, "SEQWRITE") ||
        !RunSequential(File, FALSE, 1024 * 1024, FileSize, Buffer, "SEQREAD") ||
        !RunSequential(File, TRUE, 64 * 1024, FileSize, Buffer, "SEQWRITE") ||
        !RunSequential(File, FALSE, 64 * 1024, FileSize, Buffer, "SEQREAD") ||
        !RunRandom(File, FALSE, 4096, FileSize, Buffer, "RNDREAD") ||
        !RunRandom(File, TRUE, 4096, FileSize, Buffer, "RNDWRITE"))
    {
        CloseHandle(File);
        return 1;
    }

    CloseHandle(File);
    DeleteFileA(Path);
    ReportTable(Path, FileSize);
    Report("DISKBENCH DONE\n");
    return 0;
}
