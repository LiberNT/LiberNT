/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_THREADS 64
#define DEFAULT_ARENA_MB 32
#define DEFAULT_FILE_MB 16
#define DEFAULT_TARGET_MS 1000
#define RANDOM_SLOTS 4096
#define RANDOM_BLOCK 4096
#define GENERAL_FILE_MB 128
#define GENERAL_COPY_MB 32
#define GENERAL_WRITE_MB 64
#define GENERAL_ALLOC_MB 64
#define GENERAL_CHUNK (1024u * 1024u)
#define GENERAL_SMALL_FILES 64
#define GENERAL_SMALL_BYTES 4096u
#define GENERAL_MAP_WRITES 1024
#define STORAGE_DIRECT_MB 256
#define STORAGE_COPY_MB 128
#define STORAGE_APPEND_MB 64
#define STORAGE_APPEND_CHUNK (64u * 1024u)
#define STORAGE_META_FILES 1024
#define STORAGE_CREATE_FILES 128
#define STORAGE_QUEUE_DEPTH 32
#define STORAGE_FLUSH_MB 16
#define SMP_COPY_BYTES (8u * 1024u * 1024u)
#define SMP_SECTION_BYTES (16u * 1024u * 1024u)
#define SMP_REGION_BYTES (64u * 1024u)

typedef enum
{
    UNIT_OPS,
    UNIT_PAGES,
    UNIT_MBPS
} UNIT;

#define TRIALS 3

typedef struct
{
    const char *Name;
    UNIT Unit;
    double Rate[TRIALS];
    double Overlap[TRIALS];
    ULONG Trials;
    ULONG Status;
} RESULT;

typedef struct _WORKER WORKER;
typedef ULONGLONG (*WORKFN)(WORKER *Worker, ULONG Iterations);

struct _WORKER
{
    HANDLE Thread;
    ULONG Index;
    ULONG Iterations;
    WORKFN Work;
    ULONGLONG Done;
    ULONG Status;
    HANDLE Start;
    PUCHAR Arena;
    SIZE_T ArenaBytes;
    char Path[MAX_PATH];
    HANDLE File;
    PUCHAR Buffer;
    SIZE_T BufferBytes;
    ULONG Offsets[RANDOM_SLOTS];
    char BigPath[MAX_PATH];
    char CopyPath[MAX_PATH];
    char DirPath[MAX_PATH];
    HANDLE BigFile;
    PUCHAR Chunk;
    ULONG Seed;
    char MetaDir[MAX_PATH];
    char FlushPath[MAX_PATH];
    HANDLE DirectFile;
    HANDLE AsyncFile;
    HANDLE Port;
    HANDLE FlushFile;
    HANDLE ThroughFile;
    PUCHAR Queue;
    HANDLE Ping;
    HANDLE Pong;
    HANDLE Signal;
    volatile LONG Stop;
    PUCHAR CopySource;
    PUCHAR CopyTarget;
    PUCHAR Region;
    ULONGLONG Finish;
};

static LARGE_INTEGER Frequency;
static ULONG ArenaBytes = DEFAULT_ARENA_MB * 1024u * 1024u;
static ULONG FileBytes = DEFAULT_FILE_MB * 1024u * 1024u;
static ULONG TargetMs = DEFAULT_TARGET_MS;
static ULONG ThreadCount = 1;
static ULONG PageSize = 4096;
static RESULT Results[16];
static ULONG ResultCount;
static volatile ULONG Sink;
static char SelfPath[MAX_PATH];
static ULONG GenFileMb = GENERAL_FILE_MB;
static ULONG GenCopyMb = GENERAL_COPY_MB;
static ULONG GenWriteMb = GENERAL_WRITE_MB;
static ULONG GenAllocMb = GENERAL_ALLOC_MB;
static BOOL Profile;
static double LastOverlap;

static ULONGLONG NowTicks(void)
{
    LARGE_INTEGER Counter;
    QueryPerformanceCounter(&Counter);
    return (ULONGLONG)Counter.QuadPart;
}

static ULONGLONG TicksToMs(ULONGLONG Ticks)
{
    return (Ticks * 1000ull) / (ULONGLONG)Frequency.QuadPart;
}

static ULONGLONG MmFaultDemandZero(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        PUCHAR Base;
        SIZE_T Offset;

        Base = (PUCHAR)VirtualAlloc(NULL, Worker->ArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Base == NULL)
        {
            Worker->Status = GetLastError();
            return Pages;
        }

        for (Offset = 0; Offset < Worker->ArenaBytes; Offset += PageSize)
        {
            Base[Offset] = (UCHAR)Offset;
        }
        Pages += Worker->ArenaBytes / PageSize;
        Sink += Base[0];
        VirtualFree(Base, 0, MEM_RELEASE);
    }
    return Pages;
}

static ULONGLONG MmCommitDecommit(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;
    PUCHAR Base;

    Base = (PUCHAR)VirtualAlloc(NULL, Worker->ArenaBytes, MEM_RESERVE, PAGE_NOACCESS);
    if (Base == NULL)
    {
        Worker->Status = GetLastError();
        return 0;
    }

    for (i = 0; i < Iterations; i++)
    {
        SIZE_T Offset = ((SIZE_T)i * 64u * 1024u) % Worker->ArenaBytes;
        PVOID Chunk;

        Chunk = VirtualAlloc(Base + Offset, 64u * 1024u, MEM_COMMIT, PAGE_READWRITE);
        if (Chunk == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        *(volatile UCHAR *)Chunk = 1;
        if (!VirtualFree(Chunk, 64u * 1024u, MEM_DECOMMIT))
        {
            Worker->Status = GetLastError();
            break;
        }
        Ops++;
    }

    VirtualFree(Base, 0, MEM_RELEASE);
    return Ops;
}

static ULONGLONG MmReserveRelease(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        PVOID Base = VirtualAlloc(NULL, 1024u * 1024u, MEM_RESERVE, PAGE_NOACCESS);
        if (Base == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        if (!VirtualFree(Base, 0, MEM_RELEASE))
        {
            Worker->Status = GetLastError();
            break;
        }
        Ops++;
    }
    return Ops;
}

static ULONGLONG MmSectionMapUnmap(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping;
        PUCHAR View;

        Mapping = CreateFileMappingA(Worker->File, NULL, PAGE_READONLY, 0, 0, NULL);
        if (Mapping == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 64u * 1024u);
        if (View == NULL)
        {
            Worker->Status = GetLastError();
            CloseHandle(Mapping);
            break;
        }
        Sink += View[0];
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
        Ops++;
    }
    return Ops;
}

static ULONGLONG CcWriteCached(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Bytes = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;
        SIZE_T Offset;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->File, Zero, NULL, FILE_BEGIN))
        {
            Worker->Status = GetLastError();
            break;
        }
        for (Offset = 0; Offset < FileBytes; Offset += Worker->BufferBytes)
        {
            DWORD Written = 0;
            if (!WriteFile(Worker->File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
                Written != Worker->BufferBytes)
            {
                Worker->Status = GetLastError();
                return Bytes;
            }
            Bytes += Written;
        }
    }
    return Bytes;
}

static ULONGLONG CcReadHot(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Bytes = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;
        SIZE_T Offset;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->File, Zero, NULL, FILE_BEGIN))
        {
            Worker->Status = GetLastError();
            break;
        }
        for (Offset = 0; Offset < FileBytes; Offset += Worker->BufferBytes)
        {
            DWORD Read = 0;
            if (!ReadFile(Worker->File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Read, NULL) || Read == 0)
            {
                Worker->Status = GetLastError();
                return Bytes;
            }
            Bytes += Read;
        }
        Sink += Worker->Buffer[0];
    }
    return Bytes;
}

static ULONGLONG CcReadRandomHot(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Position;
        DWORD Read = 0;

        Position.QuadPart = Worker->Offsets[i % RANDOM_SLOTS];
        if (!SetFilePointerEx(Worker->File, Position, NULL, FILE_BEGIN))
        {
            Worker->Status = GetLastError();
            break;
        }
        if (!ReadFile(Worker->File, Worker->Buffer, RANDOM_BLOCK, &Read, NULL) || Read == 0)
        {
            Worker->Status = GetLastError();
            break;
        }
        Sink += Worker->Buffer[0];
        Ops++;
    }
    return Ops;
}

static ULONGLONG CcMappedRead(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping;
        PUCHAR View;
        SIZE_T Offset;

        Mapping = CreateFileMappingA(Worker->File, NULL, PAGE_READONLY, 0, 0, NULL);
        if (Mapping == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        if (View == NULL)
        {
            Worker->Status = GetLastError();
            CloseHandle(Mapping);
            break;
        }
        for (Offset = 0; Offset < FileBytes; Offset += PageSize)
        {
            Sink += View[Offset];
            Pages++;
        }
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
    }
    return Pages;
}

static void Fail(WORKER *Worker)
{
    DWORD Error = GetLastError();

    Worker->Status = Error ? Error : ERROR_GEN_FAILURE;
}

static ULONG NextRandom(WORKER *Worker)
{
    Worker->Seed = Worker->Seed * 1664525u + 1013904223u;
    return Worker->Seed >> 8;
}

static BOOL GenWriteChunks(WORKER *Worker, HANDLE File, ULONG Megabytes)
{
    ULONG i;

    for (i = 0; i < Megabytes; i++)
    {
        DWORD Written = 0;

        if (!WriteFile(File, Worker->Chunk, GENERAL_CHUNK, &Written, NULL) || Written != GENERAL_CHUNK)
        {
            Fail(Worker);
            return FALSE;
        }
    }
    return TRUE;
}

static ULONGLONG GenAllocFillFree(WORKER *Worker, ULONG Iterations)
{
    SIZE_T Bytes = (SIZE_T)GenAllocMb * GENERAL_CHUNK;
    ULONGLONG Done = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        PUCHAR Base = (PUCHAR)VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (Base == NULL)
        {
            Fail(Worker);
            break;
        }
        memset(Base, (int)i, Bytes);
        Sink += Base[Bytes - 1];
        VirtualFree(Base, 0, MEM_RELEASE);
        Done += Bytes;
    }
    return Done;
}

static ULONGLONG GenFileSeqWrite(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Done = 0;
    ULONG i;

    sprintf(Path, "%s.w", Worker->BigPath);
    for (i = 0; i < Iterations; i++)
    {
        HANDLE File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        BOOL Ok;

        if (File == INVALID_HANDLE_VALUE)
        {
            Fail(Worker);
            break;
        }
        Ok = GenWriteChunks(Worker, File, GenWriteMb);
        if (Ok && !FlushFileBuffers(File))
        {
            Fail(Worker);
            Ok = FALSE;
        }
        CloseHandle(File);
        DeleteFileA(Path);
        if (!Ok)
            break;
        Done += (ULONGLONG)GenWriteMb * GENERAL_CHUNK;
    }
    return Done;
}

static ULONGLONG GenFileSeqRead(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Done = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->BigFile, Zero, NULL, FILE_BEGIN))
        {
            Fail(Worker);
            break;
        }
        for (j = 0; j < GenFileMb; j++)
        {
            DWORD Read = 0;

            if (!ReadFile(Worker->BigFile, Worker->Chunk, GENERAL_CHUNK, &Read, NULL) || Read != GENERAL_CHUNK)
            {
                Fail(Worker);
                return Done;
            }
            Done += Read;
        }
        Sink += Worker->Chunk[0];
    }
    return Done;
}

static ULONGLONG GenFileRandomRead(WORKER *Worker, ULONG Iterations)
{
    ULONG Blocks = GenFileMb * (GENERAL_CHUNK / RANDOM_BLOCK);
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Position;
        DWORD Read = 0;

        Position.QuadPart = (LONGLONG)(NextRandom(Worker) % Blocks) * RANDOM_BLOCK;
        if (!SetFilePointerEx(Worker->BigFile, Position, NULL, FILE_BEGIN) ||
            !ReadFile(Worker->BigFile, Worker->Chunk, RANDOM_BLOCK, &Read, NULL) || Read != RANDOM_BLOCK)
        {
            Fail(Worker);
            break;
        }
        Sink += Worker->Chunk[0];
        Ops++;
    }
    return Ops;
}

static ULONGLONG GenMapSeqRead(WORKER *Worker, ULONG Iterations)
{
    SIZE_T Bytes = (SIZE_T)GenFileMb * GENERAL_CHUNK;
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping = CreateFileMappingA(Worker->BigFile, NULL, PAGE_READONLY, 0, 0, NULL);
        PUCHAR View;
        SIZE_T Offset;

        if (Mapping == NULL)
        {
            Fail(Worker);
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        if (View == NULL)
        {
            Fail(Worker);
            CloseHandle(Mapping);
            break;
        }
        for (Offset = 0; Offset < Bytes; Offset += PageSize)
            Sink += View[Offset];
        Pages += Bytes / PageSize;
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
    }
    return Pages;
}

static ULONGLONG GenMapRandomWrite(WORKER *Worker, ULONG Iterations)
{
    ULONG Pages = (ULONG)(((SIZE_T)GenFileMb * GENERAL_CHUNK) / PageSize);
    ULONGLONG Done = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping = CreateFileMappingA(Worker->BigFile, NULL, PAGE_READWRITE, 0, 0, NULL);
        PUCHAR View;
        BOOL Ok;

        if (Mapping == NULL)
        {
            Fail(Worker);
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0);
        if (View == NULL)
        {
            Fail(Worker);
            CloseHandle(Mapping);
            break;
        }
        for (j = 0; j < GENERAL_MAP_WRITES; j++)
            *(volatile ULONG *)(View + (SIZE_T)(NextRandom(Worker) % Pages) * PageSize) = i + j;
        Ok = FlushViewOfFile(View, 0);
        if (!Ok)
            Fail(Worker);
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
        if (!Ok)
            break;
        Done += GENERAL_MAP_WRITES;
    }
    return Done;
}

static ULONGLONG GenSmallFiles(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Files = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        for (j = 0; j < GENERAL_SMALL_FILES; j++)
        {
            HANDLE File;
            DWORD Written = 0;

            sprintf(Path, "%s\\f%lu.tmp", Worker->DirPath, (unsigned long)j);
            File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (File == INVALID_HANDLE_VALUE)
            {
                Fail(Worker);
                return Files;
            }
            if (!WriteFile(File, Worker->Chunk, GENERAL_SMALL_BYTES, &Written, NULL) ||
                Written != GENERAL_SMALL_BYTES)
            {
                Fail(Worker);
                CloseHandle(File);
                return Files;
            }
            CloseHandle(File);
        }
        for (j = 0; j < GENERAL_SMALL_FILES; j++)
        {
            sprintf(Path, "%s\\f%lu.tmp", Worker->DirPath, (unsigned long)j);
            if (!DeleteFileA(Path))
            {
                Fail(Worker);
                return Files;
            }
        }
        Files += GENERAL_SMALL_FILES;
    }
    return Files;
}

static ULONGLONG GenFileCopy(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Done = 0;
    ULONG i;

    sprintf(Path, "%s.copy", Worker->CopyPath);
    for (i = 0; i < Iterations; i++)
    {
        if (!CopyFileA(Worker->CopyPath, Path, FALSE))
        {
            Fail(Worker);
            break;
        }
        DeleteFileA(Path);
        Done += (ULONGLONG)GenCopyMb * GENERAL_CHUNK;
    }
    return Done;
}

static ULONGLONG GenProcessSpawn(WORKER *Worker, ULONG Iterations)
{
    char CommandLine[MAX_PATH + 16];
    ULONGLONG Done = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        STARTUPINFOA StartupInfo;
        PROCESS_INFORMATION ProcessInfo;
        DWORD ExitCode = 1;

        sprintf(CommandLine, "\"%s\" -noop", SelfPath);
        ZeroMemory(&StartupInfo, sizeof(StartupInfo));
        StartupInfo.cb = sizeof(StartupInfo);
        if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
        {
            Fail(Worker);
            break;
        }
        WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
        GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
        if (ExitCode != 0)
        {
            Worker->Status = ERROR_GEN_FAILURE;
            break;
        }
        Done++;
    }
    return Done;
}

static DWORD WINAPI WorkerMain(LPVOID Context)
{
    WORKER *Worker = (WORKER *)Context;

    WaitForSingleObject(Worker->Start, INFINITE);
    Worker->Done = Worker->Work(Worker, Worker->Iterations);
    Worker->Finish = NowTicks();
    return 0;
}

static BOOL WorkerFileOpen(WORKER *Worker, BOOL Prefill)
{
    DWORD Written;
    SIZE_T Offset;

    Worker->File = CreateFileA(Worker->Path,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (Worker->File == INVALID_HANDLE_VALUE)
    {
        Worker->Status = GetLastError();
        Worker->File = NULL;
        return FALSE;
    }

    if (!Prefill)
        return TRUE;

    for (Offset = 0; Offset < FileBytes; Offset += Worker->BufferBytes)
    {
        if (!WriteFile(Worker->File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL))
        {
            Worker->Status = GetLastError();
            return FALSE;
        }
    }
    FlushFileBuffers(Worker->File);
    return TRUE;
}

static void WorkerFileClose(WORKER *Worker)
{
    if (Worker->File != NULL)
    {
        CloseHandle(Worker->File);
        Worker->File = NULL;
    }
    DeleteFileA(Worker->Path);
}

static ULONG RunPass(WORKER *Workers, WORKFN Work, ULONG Iterations, ULONGLONG *TotalWork, ULONGLONG *Ticks)
{
    HANDLE Handles[MAX_THREADS];
    HANDLE Start;
    ULONGLONG Begin, End, FirstFinish;
    ULONG i;
    ULONG Status = 0;

    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Start == NULL)
        return GetLastError();

    for (i = 0; i < ThreadCount; i++)
    {
        Workers[i].Work = Work;
        Workers[i].Iterations = Iterations;
        Workers[i].Done = 0;
        Workers[i].Start = Start;
        Workers[i].Thread = CreateThread(NULL, 0, WorkerMain, &Workers[i], 0, NULL);
        if (Workers[i].Thread == NULL)
        {
            CloseHandle(Start);
            return GetLastError();
        }
        Handles[i] = Workers[i].Thread;
    }

    Begin = NowTicks();
    SetEvent(Start);
    WaitForMultipleObjects(ThreadCount, Handles, TRUE, INFINITE);
    End = NowTicks();

    *TotalWork = 0;
    FirstFinish = End;
    for (i = 0; i < ThreadCount; i++)
    {
        if (Workers[i].Finish < FirstFinish)
            FirstFinish = Workers[i].Finish;
        *TotalWork += Workers[i].Done;
        if (Workers[i].Status != 0 && Status == 0)
            Status = Workers[i].Status;
        CloseHandle(Workers[i].Thread);
        Workers[i].Thread = NULL;
    }
    *Ticks = End - Begin;
    LastOverlap = (End > Begin && FirstFinish > Begin) ? (double)(FirstFinish - Begin) / (double)(End - Begin) : 0.0;
    CloseHandle(Start);
    return Status;
}

static void ReportRow(RESULT *Row);

static int CompareDouble(const void *A, const void *B)
{
    double L = *(const double *)A, R = *(const double *)B;
    return (L > R) - (L < R);
}

static double RateOf(UNIT Unit, ULONGLONG Work, ULONGLONG Ticks)
{
    double Seconds = (double)Ticks / (double)Frequency.QuadPart;

    if (Seconds <= 0.0)
        Seconds = 1e-9;
    if (Unit == UNIT_MBPS)
        return ((double)Work / (1024.0 * 1024.0)) / Seconds;
    return (double)Work / Seconds;
}

static void Measure(const char *Name, UNIT Unit, WORKER *Workers, WORKFN Work, ULONG StartIterations)
{
    ULONGLONG TotalWork = 0, Ticks = 0;
    ULONG Iterations = StartIterations;
    ULONG Status;
    ULONG Attempt, Trial;

    Status = RunPass(Workers, Work, 1, &TotalWork, &Ticks);

    for (Attempt = 0; Attempt < 12 && Status == 0; Attempt++)
    {
        Status = RunPass(Workers, Work, Iterations, &TotalWork, &Ticks);
        if (Status != 0)
            break;
        if (TicksToMs(Ticks) >= TargetMs)
            break;
        if (TicksToMs(Ticks) < 20)
            Iterations *= 8;
        else
            Iterations = (ULONG)((ULONGLONG)Iterations * TargetMs / (TicksToMs(Ticks) + 1) + 1);
    }

    Results[ResultCount].Name = Name;
    Results[ResultCount].Unit = Unit;
    Results[ResultCount].Trials = 0;
    Results[ResultCount].Status = Status;

    if (Profile)
    {
        char Message[128];

        sprintf(Message, "MMCC_PROFILE %s threads=%lu\n", Name, ThreadCount);
        OutputDebugStringA(Message);
    }

    for (Trial = 0; Trial < TRIALS && Status == 0; Trial++)
    {
        Status = RunPass(Workers, Work, Iterations, &TotalWork, &Ticks);
        if (Status != 0)
        {
            Results[ResultCount].Status = Status;
            break;
        }
        Results[ResultCount].Overlap[Results[ResultCount].Trials] = LastOverlap;
        Results[ResultCount].Rate[Results[ResultCount].Trials++] = RateOf(Unit, TotalWork, Ticks);
    }

    if (Results[ResultCount].Trials > 1)
    {
        qsort(Results[ResultCount].Rate, Results[ResultCount].Trials,
              sizeof(double), CompareDouble);
    }
    ReportRow(&Results[ResultCount]);
    fflush(stdout);
    ResultCount++;
}

static void ReportRow(RESULT *Row)
{
    const char *Unit;
    double Median, Low, High, Spread;

    if (Row->Status != 0 || Row->Trials == 0)
    {
        printf("MMCC %-22s threads=%-2lu ERROR %lu\n", Row->Name, ThreadCount, Row->Status);
        return;
    }

    switch (Row->Unit)
    {
        case UNIT_MBPS:  Unit = "MB/s"; break;
        case UNIT_PAGES: Unit = "pages/s"; break;
        default:         Unit = "ops/s"; break;
    }

    Median = Row->Rate[Row->Trials / 2];
    Low = Row->Rate[0];
    High = Row->Rate[Row->Trials - 1];
    Spread = (Low > 0.0) ? (High / Low) : 0.0;

    printf("MMCC %-22s threads=%-2lu %14.1f %-8s (n=%lu min=%.1f max=%.1f spread=%.2fx)",
           Row->Name, ThreadCount, Median, Unit,
           (unsigned long)Row->Trials, Low, High, Spread);
    if (ThreadCount > 1)
    {
        double Overlap = Row->Overlap[0];
        ULONG Trial;

        for (Trial = 1; Trial < Row->Trials; Trial++)
        {
            if (Row->Overlap[Trial] < Overlap)
                Overlap = Row->Overlap[Trial];
        }
        printf(" overlap=%.2f", Overlap);
    }
    printf("\n");
}

static BOOL GeneralOpen(WORKER *Worker, const char *TempPath)
{
    HANDLE File;

    Worker->Seed = 0x9E3779B9u * (Worker->Index + 1);
    Worker->Chunk = (PUCHAR)VirtualAlloc(NULL, GENERAL_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Worker->Chunk == NULL)
    {
        Fail(Worker);
        return FALSE;
    }
    memset(Worker->Chunk, (int)(Worker->Index + 1), GENERAL_CHUNK);

    sprintf(Worker->BigPath, "%smmccg%lu.dat", TempPath, (unsigned long)Worker->Index);
    sprintf(Worker->CopyPath, "%smmccc%lu.dat", TempPath, (unsigned long)Worker->Index);
    sprintf(Worker->DirPath, "%smmccd%lu", TempPath, (unsigned long)Worker->Index);
    if (!CreateDirectoryA(Worker->DirPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        Fail(Worker);
        return FALSE;
    }

    File = CreateFileA(Worker->CopyPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Fail(Worker);
        return FALSE;
    }
    if (!GenWriteChunks(Worker, File, GenCopyMb))
    {
        CloseHandle(File);
        return FALSE;
    }
    CloseHandle(File);

    Worker->BigFile = CreateFileA(Worker->BigPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Worker->BigFile == INVALID_HANDLE_VALUE)
    {
        Worker->BigFile = NULL;
        Fail(Worker);
        return FALSE;
    }
    if (!GenWriteChunks(Worker, Worker->BigFile, GenFileMb))
        return FALSE;
    FlushFileBuffers(Worker->BigFile);
    return TRUE;
}

static void GeneralClose(WORKER *Worker)
{
    char Path[MAX_PATH + 32];
    ULONG j;

    if (Worker->BigFile != NULL)
    {
        CloseHandle(Worker->BigFile);
        Worker->BigFile = NULL;
    }
    if (Worker->DirPath[0] != '\0')
    {
        DeleteFileA(Worker->BigPath);
        DeleteFileA(Worker->CopyPath);
        for (j = 0; j < GENERAL_SMALL_FILES; j++)
        {
            sprintf(Path, "%s\\f%lu.tmp", Worker->DirPath, (unsigned long)j);
            DeleteFileA(Path);
        }
        RemoveDirectoryA(Worker->DirPath);
    }
    if (Worker->Chunk != NULL)
    {
        VirtualFree(Worker->Chunk, 0, MEM_RELEASE);
        Worker->Chunk = NULL;
    }
}

static const struct
{
    const char *Name;
    UNIT Unit;
    WORKFN Work;
    ULONG Start;
} GeneralTests[] =
{
    { "gen_alloc_fill_free", UNIT_MBPS, GenAllocFillFree, 1 },
    { "gen_file_seq_write", UNIT_MBPS, GenFileSeqWrite, 1 },
    { "gen_file_seq_read", UNIT_MBPS, GenFileSeqRead, 1 },
    { "gen_file_rand_read", UNIT_OPS, GenFileRandomRead, 256 },
    { "gen_map_seq_read", UNIT_PAGES, GenMapSeqRead, 1 },
    { "gen_map_rand_write", UNIT_PAGES, GenMapRandomWrite, 1 },
    { "gen_small_files", UNIT_OPS, GenSmallFiles, 1 },
    { "gen_file_copy", UNIT_MBPS, GenFileCopy, 1 },
    { "gen_process_spawn", UNIT_OPS, GenProcessSpawn, 1 },
};

static ULONG GeneralShare(ULONG Megabytes)
{
    ULONG Share = Megabytes / ThreadCount;

    return Share ? Share : 1;
}

static BOOL GenCheckFile(WORKER *Worker, const char *Path, ULONG Blocks, BOOL Distinct)
{
    HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER Size;
    ULONG b, k;

    if (File == INVALID_HANDLE_VALUE)
    {
        printf("MMCC_VERIFY FAIL open %s %lu\n", Path, GetLastError());
        return FALSE;
    }
    if (!GetFileSizeEx(File, &Size) || Size.QuadPart != (LONGLONG)Blocks * Worker->BufferBytes)
    {
        printf("MMCC_VERIFY FAIL size %s %I64d\n", Path, Size.QuadPart);
        CloseHandle(File);
        return FALSE;
    }
    for (b = 0; b < Blocks; b++)
    {
        UCHAR Expect = Distinct ? (UCHAR)(b * 7 + 1) : (UCHAR)(Worker->Index + 1);
        DWORD Read = 0;

        if (!ReadFile(File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Read, NULL) || Read != Worker->BufferBytes)
        {
            printf("MMCC_VERIFY FAIL read %s block %lu %lu\n", Path, b, GetLastError());
            CloseHandle(File);
            return FALSE;
        }
        for (k = 0; k < Worker->BufferBytes; k++)
        {
            if (Worker->Buffer[k] != Expect)
            {
                printf("MMCC_VERIFY FAIL data %s offset %lu got %u want %u\n", Path,
                       (unsigned long)(b * Worker->BufferBytes + k), Worker->Buffer[k], Expect);
                CloseHandle(File);
                return FALSE;
            }
        }
    }
    CloseHandle(File);
    return TRUE;
}

static BOOL GenVerifyRename(WORKER *Worker)
{
    char OldPath[MAX_PATH + 32];
    char NewPath[MAX_PATH + 32];
    HANDLE First = INVALID_HANDLE_VALUE;
    HANDLE Second = INVALID_HANDLE_VALUE;
    HANDLE Renamed = INVALID_HANDLE_VALUE;
    DWORD Sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD Written;
    LARGE_INTEGER Position;
    const char *Stage = "create";
    BOOL Ok = FALSE;

    sprintf(OldPath, "%s.rename-old", Worker->BigPath);
    sprintf(NewPath, "%s.rename-new", Worker->BigPath);
    First = CreateFileA(OldPath, GENERIC_READ | GENERIC_WRITE, Sharing, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (First == INVALID_HANDLE_VALUE)
        goto Done;
    Stage = "initial write";
    memset(Worker->Buffer, 1, Worker->BufferBytes);
    if (!WriteFile(First, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
        Written != Worker->BufferBytes)
        goto Done;
    Stage = "second open";
    Second = CreateFileA(OldPath, GENERIC_READ | GENERIC_WRITE, Sharing, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Second == INVALID_HANDLE_VALUE)
        goto Done;
    Stage = "pending size";
    if (!GetFileSizeEx(Second, &Position) || Position.QuadPart != Worker->BufferBytes)
        goto Done;
    Stage = "rename";
    if (!MoveFileExA(OldPath, NewPath, MOVEFILE_REPLACE_EXISTING))
        goto Done;
    Stage = "renamed open";
    Renamed = CreateFileA(NewPath, GENERIC_READ | GENERIC_WRITE, Sharing, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Renamed == INVALID_HANDLE_VALUE)
        goto Done;
    Stage = "old handle extension";
    Position.QuadPart = Worker->BufferBytes;
    memset(Worker->Buffer, 8, Worker->BufferBytes);
    if (!SetFilePointerEx(Second, Position, NULL, FILE_BEGIN) ||
        !WriteFile(Second, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
        Written != Worker->BufferBytes)
        goto Done;
    CloseHandle(First);
    First = INVALID_HANDLE_VALUE;
    CloseHandle(Second);
    Second = INVALID_HANDLE_VALUE;
    Stage = "flush through renamed handle";
    if (!FlushFileBuffers(Renamed))
        goto Done;
    Stage = "shared final size";
    if (!GetFileSizeEx(Renamed, &Position) || Position.QuadPart != 2 * Worker->BufferBytes)
        goto Done;
    CloseHandle(Renamed);
    Renamed = INVALID_HANDLE_VALUE;
    Stage = "renamed data";
    Ok = GenCheckFile(Worker, NewPath, 2, TRUE);

Done:
    if (!Ok)
        printf("MMCC_VERIFY FAIL rename %s %lu\n", Stage, GetLastError());
    if (First != INVALID_HANDLE_VALUE)
        CloseHandle(First);
    if (Second != INVALID_HANDLE_VALUE)
        CloseHandle(Second);
    if (Renamed != INVALID_HANDLE_VALUE)
        CloseHandle(Renamed);
    DeleteFileA(OldPath);
    DeleteFileA(NewPath);
    return Ok;
}

static BOOL GeneralVerify(WORKER *Worker)
{
    char Path[MAX_PATH + 32];
    ULONG Blocks = 256;
    ULONG b;
    HANDLE File;
    BOOL Ok;

    sprintf(Path, "%s.v", Worker->BigPath);
    File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        printf("MMCC_VERIFY FAIL create %s %lu\n", Path, GetLastError());
        return FALSE;
    }
    for (b = 0; b < Blocks; b++)
    {
        DWORD Written = 0;

        memset(Worker->Buffer, (int)(UCHAR)(b * 7 + 1), Worker->BufferBytes);
        if (!WriteFile(File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
            Written != Worker->BufferBytes)
        {
            printf("MMCC_VERIFY FAIL write %s block %lu %lu\n", Path, b, GetLastError());
            CloseHandle(File);
            return FALSE;
        }
    }
    CloseHandle(File);
    Ok = GenCheckFile(Worker, Path, Blocks, TRUE);
    DeleteFileA(Path);
    if (!Ok)
        return FALSE;

    sprintf(Path, "%s.vcopy", Worker->CopyPath);
    if (!CopyFileA(Worker->CopyPath, Path, FALSE))
    {
        printf("MMCC_VERIFY FAIL copy %s %lu\n", Path, GetLastError());
        return FALSE;
    }
    Ok = GenCheckFile(Worker, Path, (ULONG)(((ULONGLONG)GenCopyMb * GENERAL_CHUNK) / Worker->BufferBytes), FALSE);
    DeleteFileA(Path);
    if (!Ok || !GenVerifyRename(Worker))
        return FALSE;
    {
        WORKER Small = *Worker;

        Small.BufferBytes = 4096;
        return GenVerifyRename(&Small);
    }
}

static BOOL TestSelected(const char *Name, const char *Only)
{
    char Token[64];
    size_t Length;

    if (Only == NULL)
        return TRUE;
    while (*Only != '\0')
    {
        Length = strcspn(Only, ",");
        if (Length != 0 && Length < sizeof(Token))
        {
            memcpy(Token, Only, Length);
            Token[Length] = '\0';
            if (strstr(Name, Token) != NULL)
                return TRUE;
        }
        Only += Length;
        if (*Only == ',')
            Only++;
    }
    return FALSE;
}

static int RunGeneralSuite(WORKER *Workers, const char *TempPath, const char *Only)
{
    ULONG i;
    int Result = 0;

    GenFileMb = GeneralShare(GENERAL_FILE_MB);
    GenCopyMb = GeneralShare(GENERAL_COPY_MB);
    GenWriteMb = GeneralShare(GENERAL_WRITE_MB);
    GenAllocMb = GeneralShare(GENERAL_ALLOC_MB);

    for (i = 0; i < ThreadCount; i++)
    {
        if (!GeneralOpen(&Workers[i], TempPath))
        {
            printf("MMCC_ERROR general setup %lu\n", Workers[i].Status);
            Result = 1;
            break;
        }
    }

    if (Result == 0)
    {
        for (i = 0; i < sizeof(GeneralTests) / sizeof(GeneralTests[0]); i++)
        {
            if (TestSelected(GeneralTests[i].Name, Only))
                Measure(GeneralTests[i].Name, GeneralTests[i].Unit, Workers, GeneralTests[i].Work, GeneralTests[i].Start);
        }
    }

    if (Result == 0)
    {
        for (i = 0; i < ThreadCount; i++)
        {
            if (!GeneralVerify(&Workers[i]))
            {
                Result = 1;
                break;
            }
        }
        printf("MMCC_VERIFY %s\n", Result == 0 ? "ok" : "failed");
    }

    for (i = 0; i < ThreadCount; i++)
        GeneralClose(&Workers[i]);
    return Result;
}

static ULONG StDirectMb;
static ULONG StAppendMb;
static ULONG StMetaFiles;
static ULONG StCreateFiles;
static CRITICAL_SECTION SharedLock;
static SRWLOCK SharedSrw = SRWLOCK_INIT;
static volatile LONG SharedCounter;
static ULONG SharedValue;

static ULONGLONG StSequential(WORKER *Worker, ULONG Iterations, BOOL Write)
{
    ULONGLONG Done = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->DirectFile, Zero, NULL, FILE_BEGIN))
        {
            Fail(Worker);
            break;
        }
        for (j = 0; j < StDirectMb; j++)
        {
            DWORD Bytes = 0;
            BOOL Ok = Write ? WriteFile(Worker->DirectFile, Worker->Chunk, GENERAL_CHUNK, &Bytes, NULL)
                            : ReadFile(Worker->DirectFile, Worker->Chunk, GENERAL_CHUNK, &Bytes, NULL);

            if (!Ok || Bytes != GENERAL_CHUNK)
            {
                Fail(Worker);
                return Done;
            }
            Done += Bytes;
        }
    }
    return Done;
}

static ULONGLONG StDirectSeqWrite(WORKER *Worker, ULONG Iterations)
{
    return StSequential(Worker, Iterations, TRUE);
}

static ULONGLONG StDirectSeqRead(WORKER *Worker, ULONG Iterations)
{
    return StSequential(Worker, Iterations, FALSE);
}

static ULONGLONG StRandom(WORKER *Worker, HANDLE File, ULONG Iterations, ULONG Blocks, BOOL Write)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Position;
        DWORD Bytes = 0;
        BOOL Ok;

        Position.QuadPart = (LONGLONG)(NextRandom(Worker) % Blocks) * RANDOM_BLOCK;
        Ok = SetFilePointerEx(File, Position, NULL, FILE_BEGIN);
        if (Ok)
        {
            Ok = Write ? WriteFile(File, Worker->Chunk, RANDOM_BLOCK, &Bytes, NULL)
                       : ReadFile(File, Worker->Chunk, RANDOM_BLOCK, &Bytes, NULL);
        }
        if (!Ok || Bytes != RANDOM_BLOCK)
        {
            Fail(Worker);
            break;
        }
        Ops++;
    }
    Sink += Worker->Chunk[0];
    return Ops;
}

static ULONGLONG StDirectRandRead(WORKER *Worker, ULONG Iterations)
{
    return StRandom(Worker, Worker->DirectFile, Iterations, StDirectMb * (GENERAL_CHUNK / RANDOM_BLOCK), FALSE);
}

static ULONGLONG StDirectRandWrite(WORKER *Worker, ULONG Iterations)
{
    return StRandom(Worker, Worker->DirectFile, Iterations, StDirectMb * (GENERAL_CHUNK / RANDOM_BLOCK), TRUE);
}

static ULONGLONG StWriteThrough(WORKER *Worker, ULONG Iterations)
{
    return StRandom(Worker, Worker->ThroughFile, Iterations, STORAGE_FLUSH_MB * (GENERAL_CHUNK / RANDOM_BLOCK), TRUE);
}

static BOOL StIssueRead(WORKER *Worker, OVERLAPPED *Request, ULONG Slot, ULONG Blocks)
{
    ULONGLONG Offset = (ULONGLONG)(NextRandom(Worker) % Blocks) * RANDOM_BLOCK;

    ZeroMemory(Request, sizeof(*Request));
    Request->Offset = (DWORD)Offset;
    Request->OffsetHigh = (DWORD)(Offset >> 32);
    if (ReadFile(Worker->AsyncFile, Worker->Queue + (SIZE_T)Slot * RANDOM_BLOCK, RANDOM_BLOCK, NULL, Request) ||
        GetLastError() == ERROR_IO_PENDING)
    {
        return TRUE;
    }
    Fail(Worker);
    return FALSE;
}

static ULONGLONG StDirectRandReadQueued(WORKER *Worker, ULONG Iterations)
{
    OVERLAPPED Requests[STORAGE_QUEUE_DEPTH];
    ULONG Blocks = StDirectMb * (GENERAL_CHUNK / RANDOM_BLOCK);
    ULONG Issued = 0, Completed = 0, Slot;

    for (Slot = 0; Slot < STORAGE_QUEUE_DEPTH && Issued < Iterations; Slot++)
    {
        if (!StIssueRead(Worker, &Requests[Slot], Slot, Blocks))
            break;
        Issued++;
    }
    if (Worker->Status != 0)
        Iterations = Issued;

    while (Completed < Issued)
    {
        LPOVERLAPPED Done = NULL;
        ULONG_PTR Key;
        DWORD Bytes = 0;

        if (!GetQueuedCompletionStatus(Worker->Port, &Bytes, &Key, &Done, INFINITE))
        {
            Fail(Worker);
            if (Done == NULL)
                break;
            Iterations = Issued;
        }
        else if (Bytes != RANDOM_BLOCK)
        {
            Worker->Status = ERROR_GEN_FAILURE;
            Iterations = Issued;
        }
        Completed++;
        if (Issued < Iterations)
        {
            Slot = (ULONG)(Done - Requests);
            if (StIssueRead(Worker, &Requests[Slot], Slot, Blocks))
                Issued++;
            else
                Iterations = Issued;
        }
    }
    Sink += Worker->Queue[0];
    return Completed;
}

static ULONGLONG StFlush(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Position;
        DWORD Written = 0;

        Position.QuadPart = (LONGLONG)(i % 256) * RANDOM_BLOCK;
        if (!SetFilePointerEx(Worker->FlushFile, Position, NULL, FILE_BEGIN) ||
            !WriteFile(Worker->FlushFile, Worker->Chunk, RANDOM_BLOCK, &Written, NULL) || Written != RANDOM_BLOCK ||
            !FlushFileBuffers(Worker->FlushFile))
        {
            Fail(Worker);
            break;
        }
        Ops++;
    }
    return Ops;
}

static ULONGLONG StAppend(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Done = 0;
    ULONG i, j;

    sprintf(Path, "%s.a", Worker->BigPath);
    for (i = 0; i < Iterations; i++)
    {
        HANDLE File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        BOOL Ok = TRUE;

        if (File == INVALID_HANDLE_VALUE)
        {
            Fail(Worker);
            break;
        }
        for (j = 0; Ok && j < StAppendMb * (GENERAL_CHUNK / STORAGE_APPEND_CHUNK); j++)
        {
            DWORD Written = 0;

            Ok = WriteFile(File, Worker->Chunk, STORAGE_APPEND_CHUNK, &Written, NULL) && Written == STORAGE_APPEND_CHUNK;
        }
        if (!Ok)
            Fail(Worker);
        CloseHandle(File);
        DeleteFileA(Path);
        if (!Ok)
            break;
        Done += (ULONGLONG)StAppendMb * GENERAL_CHUNK;
    }
    return Done;
}

static ULONGLONG StMetaCreateDelete(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Files = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        for (j = 0; j < StCreateFiles; j++)
        {
            HANDLE File;

            sprintf(Path, "%s\\c%lu.tmp", Worker->DirPath, (unsigned long)j);
            File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (File == INVALID_HANDLE_VALUE)
            {
                Fail(Worker);
                return Files;
            }
            CloseHandle(File);
        }
        for (j = 0; j < StCreateFiles; j++)
        {
            sprintf(Path, "%s\\c%lu.tmp", Worker->DirPath, (unsigned long)j);
            if (!DeleteFileA(Path))
            {
                Fail(Worker);
                return Files;
            }
        }
        Files += StCreateFiles;
    }
    return Files;
}

static ULONGLONG StMetaEnum(WORKER *Worker, ULONG Iterations)
{
    char Pattern[MAX_PATH + 32];
    ULONGLONG Entries = 0;
    ULONG i;

    sprintf(Pattern, "%s\\*", Worker->MetaDir);
    for (i = 0; i < Iterations; i++)
    {
        WIN32_FIND_DATAA Data;
        HANDLE Find = FindFirstFileA(Pattern, &Data);
        ULONG Count = 0;

        if (Find == INVALID_HANDLE_VALUE)
        {
            Fail(Worker);
            break;
        }
        do
        {
            Count++;
        } while (FindNextFileA(Find, &Data));
        FindClose(Find);
        if (Count < StMetaFiles)
        {
            Worker->Status = ERROR_GEN_FAILURE;
            break;
        }
        Entries += Count;
    }
    return Entries;
}

static ULONGLONG StMetaStat(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        WIN32_FILE_ATTRIBUTE_DATA Data;

        sprintf(Path, "%s\\m%05lu.dat", Worker->MetaDir, (unsigned long)(NextRandom(Worker) % StMetaFiles));
        if (!GetFileAttributesExA(Path, GetFileExInfoStandard, &Data))
        {
            Fail(Worker);
            break;
        }
        Ops++;
    }
    return Ops;
}

static ULONGLONG StMetaOpenClose(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH + 32];
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE File;

        sprintf(Path, "%s\\m%05lu.dat", Worker->MetaDir, (unsigned long)(NextRandom(Worker) % StMetaFiles));
        File = CreateFileA(Path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (File == INVALID_HANDLE_VALUE)
        {
            Fail(Worker);
            break;
        }
        CloseHandle(File);
        Ops++;
    }
    return Ops;
}

static ULONGLONG StMetaRename(WORKER *Worker, ULONG Iterations)
{
    char From[MAX_PATH + 32];
    char To[MAX_PATH + 32];
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        ULONG Index = i % StMetaFiles;

        sprintf(From, "%s\\m%05lu.dat", Worker->MetaDir, (unsigned long)Index);
        sprintf(To, "%s\\r%05lu.dat", Worker->MetaDir, (unsigned long)Index);
        if (!MoveFileA(From, To))
        {
            Fail(Worker);
            break;
        }
        if (!MoveFileA(To, From))
        {
            Fail(Worker);
            break;
        }
        Ops += 2;
    }
    return Ops;
}

static HANDLE StCreateFilled(WORKER *Worker, const char *Path, ULONG Megabytes, DWORD Flags)
{
    HANDLE File = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (File == INVALID_HANDLE_VALUE)
    {
        Fail(Worker);
        return NULL;
    }
    if (!GenWriteChunks(Worker, File, Megabytes) || !FlushFileBuffers(File))
    {
        Fail(Worker);
        CloseHandle(File);
        return NULL;
    }
    if (Flags == FILE_ATTRIBUTE_NORMAL)
        return File;
    CloseHandle(File);

    File = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, Flags, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Fail(Worker);
        return NULL;
    }
    return File;
}

static BOOL StorageOpen(WORKER *Worker, const char *Dir)
{
    char Path[MAX_PATH + 32];
    HANDLE File;
    ULONG j;

    Worker->Seed = 0x9E3779B9u * (Worker->Index + 1);
    Worker->Chunk = (PUCHAR)VirtualAlloc(NULL, GENERAL_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Worker->Queue = (PUCHAR)VirtualAlloc(NULL, STORAGE_QUEUE_DEPTH * RANDOM_BLOCK, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    if (Worker->Chunk == NULL || Worker->Queue == NULL)
    {
        Fail(Worker);
        return FALSE;
    }
    memset(Worker->Chunk, (int)(Worker->Index + 1), GENERAL_CHUNK);

    sprintf(Worker->BigPath, "%smmccs%lu.dat", Dir, (unsigned long)Worker->Index);
    sprintf(Worker->CopyPath, "%smmccc%lu.dat", Dir, (unsigned long)Worker->Index);
    sprintf(Worker->FlushPath, "%smmccf%lu.dat", Dir, (unsigned long)Worker->Index);
    sprintf(Worker->DirPath, "%smmccd%lu", Dir, (unsigned long)Worker->Index);
    sprintf(Worker->MetaDir, "%smmccm%lu", Dir, (unsigned long)Worker->Index);
    if ((!CreateDirectoryA(Worker->DirPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) ||
        (!CreateDirectoryA(Worker->MetaDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS))
    {
        Fail(Worker);
        return FALSE;
    }

    File = StCreateFilled(Worker, Worker->CopyPath, GenCopyMb, FILE_ATTRIBUTE_NORMAL);
    if (File == NULL)
        return FALSE;
    CloseHandle(File);

    Worker->DirectFile = StCreateFilled(Worker, Worker->BigPath, StDirectMb, FILE_FLAG_NO_BUFFERING);
    if (Worker->DirectFile == NULL)
        return FALSE;
    Worker->AsyncFile = CreateFileA(Worker->BigPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                    OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
    if (Worker->AsyncFile == INVALID_HANDLE_VALUE)
    {
        Worker->AsyncFile = NULL;
        Fail(Worker);
        return FALSE;
    }
    Worker->Port = CreateIoCompletionPort(Worker->AsyncFile, NULL, 0, 1);
    if (Worker->Port == NULL)
    {
        Fail(Worker);
        return FALSE;
    }

    Worker->FlushFile = StCreateFilled(Worker, Worker->FlushPath, STORAGE_FLUSH_MB, FILE_ATTRIBUTE_NORMAL);
    if (Worker->FlushFile == NULL)
        return FALSE;
    Worker->ThroughFile = CreateFileA(Worker->FlushPath, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                      FILE_FLAG_WRITE_THROUGH, NULL);
    if (Worker->ThroughFile == INVALID_HANDLE_VALUE)
    {
        Worker->ThroughFile = NULL;
        Fail(Worker);
        return FALSE;
    }

    for (j = 0; j < StMetaFiles; j++)
    {
        sprintf(Path, "%s\\m%05lu.dat", Worker->MetaDir, (unsigned long)j);
        File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (File == INVALID_HANDLE_VALUE)
        {
            Fail(Worker);
            return FALSE;
        }
        CloseHandle(File);
    }
    return TRUE;
}

static void CloseIf(HANDLE *Handle)
{
    if (*Handle != NULL)
    {
        CloseHandle(*Handle);
        *Handle = NULL;
    }
}

static void StorageClose(WORKER *Worker)
{
    char Path[MAX_PATH + 32];
    ULONG j;

    CloseIf(&Worker->Port);
    CloseIf(&Worker->AsyncFile);
    CloseIf(&Worker->DirectFile);
    CloseIf(&Worker->ThroughFile);
    CloseIf(&Worker->FlushFile);
    if (Worker->MetaDir[0] != '\0')
    {
        DeleteFileA(Worker->BigPath);
        DeleteFileA(Worker->CopyPath);
        DeleteFileA(Worker->FlushPath);
        for (j = 0; j < StCreateFiles; j++)
        {
            sprintf(Path, "%s\\c%lu.tmp", Worker->DirPath, (unsigned long)j);
            DeleteFileA(Path);
        }
        RemoveDirectoryA(Worker->DirPath);
        for (j = 0; j < StMetaFiles; j++)
        {
            sprintf(Path, "%s\\m%05lu.dat", Worker->MetaDir, (unsigned long)j);
            DeleteFileA(Path);
        }
        RemoveDirectoryA(Worker->MetaDir);
    }
    if (Worker->Queue != NULL)
    {
        VirtualFree(Worker->Queue, 0, MEM_RELEASE);
        Worker->Queue = NULL;
    }
    if (Worker->Chunk != NULL)
    {
        VirtualFree(Worker->Chunk, 0, MEM_RELEASE);
        Worker->Chunk = NULL;
    }
}

static const struct
{
    const char *Name;
    UNIT Unit;
    WORKFN Work;
    ULONG Start;
} StorageTests[] =
{
    { "st_direct_seq_write", UNIT_MBPS, StDirectSeqWrite, 1 },
    { "st_direct_seq_read", UNIT_MBPS, StDirectSeqRead, 1 },
    { "st_direct_rand_read_4k", UNIT_OPS, StDirectRandRead, 256 },
    { "st_direct_rand_write_4k", UNIT_OPS, StDirectRandWrite, 256 },
    { "st_direct_rand_read_qd32", UNIT_OPS, StDirectRandReadQueued, 1024 },
    { "st_write_through_4k", UNIT_OPS, StWriteThrough, 64 },
    { "st_flush_4k", UNIT_OPS, StFlush, 16 },
    { "st_append_64k", UNIT_MBPS, StAppend, 1 },
    { "st_copy_big", UNIT_MBPS, GenFileCopy, 1 },
    { "st_meta_create_delete", UNIT_OPS, StMetaCreateDelete, 1 },
    { "st_meta_enum", UNIT_OPS, StMetaEnum, 1 },
    { "st_meta_stat", UNIT_OPS, StMetaStat, 256 },
    { "st_meta_open_close", UNIT_OPS, StMetaOpenClose, 256 },
    { "st_meta_rename", UNIT_OPS, StMetaRename, 64 },
};

static int RunStorageSuite(WORKER *Workers, const char *Dir, const char *Only)
{
    ULONG i;
    int Result = 0;

    StDirectMb = GeneralShare(STORAGE_DIRECT_MB);
    StAppendMb = GeneralShare(STORAGE_APPEND_MB);
    GenCopyMb = GeneralShare(STORAGE_COPY_MB);
    StMetaFiles = GeneralShare(STORAGE_META_FILES);
    StCreateFiles = GeneralShare(STORAGE_CREATE_FILES);

    for (i = 0; i < ThreadCount; i++)
    {
        if (!StorageOpen(&Workers[i], Dir))
        {
            printf("MMCC_ERROR storage setup %lu\n", Workers[i].Status);
            Result = 1;
            break;
        }
    }

    if (Result == 0)
    {
        for (i = 0; i < sizeof(StorageTests) / sizeof(StorageTests[0]); i++)
        {
            if (TestSelected(StorageTests[i].Name, Only))
                Measure(StorageTests[i].Name, StorageTests[i].Unit, Workers, StorageTests[i].Work, StorageTests[i].Start);
        }
    }

    for (i = 0; i < ThreadCount; i++)
        StorageClose(&Workers[i]);
    return Result;
}

static DWORD WINAPI SmpPartner(LPVOID Context)
{
    WORKER *Worker = (WORKER *)Context;

    for (;;)
    {
        if (WaitForSingleObject(Worker->Ping, INFINITE) != WAIT_OBJECT_0 || Worker->Stop)
            break;
        SetEvent(Worker->Pong);
    }
    return 0;
}

static ULONGLONG SmpEventPingPong(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Trips = 0;
    HANDLE Partner;
    ULONG i;

    InterlockedExchange(&Worker->Stop, 0);
    Partner = CreateThread(NULL, 0, SmpPartner, Worker, 0, NULL);
    if (Partner == NULL)
    {
        Fail(Worker);
        return 0;
    }
    for (i = 0; i < Iterations; i++)
    {
        if (!SetEvent(Worker->Ping) || WaitForSingleObject(Worker->Pong, INFINITE) != WAIT_OBJECT_0)
        {
            Fail(Worker);
            break;
        }
        Trips++;
    }
    InterlockedExchange(&Worker->Stop, 1);
    SetEvent(Worker->Ping);
    WaitForSingleObject(Partner, INFINITE);
    CloseHandle(Partner);
    return Trips;
}

static DWORD WINAPI SmpEmptyThread(LPVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return 0;
}

static ULONGLONG SmpThreadCreate(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Thread = CreateThread(NULL, 0, SmpEmptyThread, NULL, 0, NULL);

        if (Thread == NULL)
        {
            Fail(Worker);
            break;
        }
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
        Ops++;
    }
    return Ops;
}

static ULONGLONG SmpCriticalSection(WORKER *Worker, ULONG Iterations)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Worker);
    for (i = 0; i < Iterations; i++)
    {
        EnterCriticalSection(&SharedLock);
        SharedValue++;
        LeaveCriticalSection(&SharedLock);
    }
    return Iterations;
}

static ULONGLONG SmpSrwShared(WORKER *Worker, ULONG Iterations)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Worker);
    for (i = 0; i < Iterations; i++)
    {
        AcquireSRWLockShared(&SharedSrw);
        Sink += SharedValue;
        ReleaseSRWLockShared(&SharedSrw);
    }
    return Iterations;
}

static ULONGLONG SmpSrwExclusive(WORKER *Worker, ULONG Iterations)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Worker);
    for (i = 0; i < Iterations; i++)
    {
        AcquireSRWLockExclusive(&SharedSrw);
        SharedValue++;
        ReleaseSRWLockExclusive(&SharedSrw);
    }
    return Iterations;
}

static ULONGLONG SmpInterlocked(WORKER *Worker, ULONG Iterations)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Worker);
    for (i = 0; i < Iterations; i++)
        InterlockedIncrement(&SharedCounter);
    return Iterations;
}

static ULONGLONG SmpHeap(WORKER *Worker, ULONG Iterations)
{
    HANDLE Heap = GetProcessHeap();
    PUCHAR Blocks[64];
    ULONGLONG Ops = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        for (j = 0; j < 64; j++)
        {
            Blocks[j] = (PUCHAR)HeapAlloc(Heap, 0, (SIZE_T)16 << ((i + j) % 9));
            if (Blocks[j] == NULL)
            {
                Fail(Worker);
                break;
            }
            Blocks[j][0] = (UCHAR)j;
        }
        while (j > 0)
            HeapFree(Heap, 0, Blocks[--j]);
        if (Worker->Status != 0)
            break;
        Ops += 64;
    }
    return Ops;
}

static ULONGLONG SmpMemcpy(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Done = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        memcpy(Worker->CopyTarget, Worker->CopySource, SMP_COPY_BYTES);
        Sink += Worker->CopyTarget[i % SMP_COPY_BYTES];
        Done += SMP_COPY_BYTES;
    }
    return Done;
}

static ULONGLONG SmpSwitchToThread(WORKER *Worker, ULONG Iterations)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Worker);
    for (i = 0; i < Iterations; i++)
        SwitchToThread();
    return Iterations;
}

static ULONGLONG SmpSyscall(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        if (!SetEvent(Worker->Signal))
        {
            Fail(Worker);
            break;
        }
        Ops++;
    }
    return Ops;
}

static ULONGLONG SmpSharedSection(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SMP_SECTION_BYTES, NULL);
        PUCHAR View;
        SIZE_T Offset;

        if (Mapping == NULL)
        {
            Fail(Worker);
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0);
        if (View == NULL)
        {
            Fail(Worker);
            CloseHandle(Mapping);
            break;
        }
        for (Offset = 0; Offset < SMP_SECTION_BYTES; Offset += PageSize)
            View[Offset] = (UCHAR)Offset;
        Pages += SMP_SECTION_BYTES / PageSize;
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
    }
    return Pages;
}

static ULONGLONG SmpVirtualProtect(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    DWORD Old;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        if (!VirtualProtect(Worker->Region, SMP_REGION_BYTES, (i & 1) ? PAGE_READWRITE : PAGE_READONLY, &Old))
        {
            Fail(Worker);
            break;
        }
        Ops++;
    }
    VirtualProtect(Worker->Region, SMP_REGION_BYTES, PAGE_READWRITE, &Old);
    return Ops;
}

static BOOL SmpOpen(WORKER *Worker)
{
    Worker->Ping = CreateEventW(NULL, FALSE, FALSE, NULL);
    Worker->Pong = CreateEventW(NULL, FALSE, FALSE, NULL);
    Worker->Signal = CreateEventW(NULL, TRUE, TRUE, NULL);
    Worker->CopySource = (PUCHAR)VirtualAlloc(NULL, SMP_COPY_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Worker->CopyTarget = (PUCHAR)VirtualAlloc(NULL, SMP_COPY_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Worker->Region = (PUCHAR)VirtualAlloc(NULL, SMP_REGION_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Worker->Ping == NULL || Worker->Pong == NULL || Worker->Signal == NULL ||
        Worker->CopySource == NULL || Worker->CopyTarget == NULL || Worker->Region == NULL)
    {
        Fail(Worker);
        return FALSE;
    }
    memset(Worker->CopySource, (int)(Worker->Index + 1), SMP_COPY_BYTES);
    memset(Worker->CopyTarget, 0, SMP_COPY_BYTES);
    memset(Worker->Region, 0, SMP_REGION_BYTES);
    return TRUE;
}

static void SmpClose(WORKER *Worker)
{
    CloseIf(&Worker->Ping);
    CloseIf(&Worker->Pong);
    CloseIf(&Worker->Signal);
    if (Worker->CopySource != NULL)
        VirtualFree(Worker->CopySource, 0, MEM_RELEASE);
    if (Worker->CopyTarget != NULL)
        VirtualFree(Worker->CopyTarget, 0, MEM_RELEASE);
    if (Worker->Region != NULL)
        VirtualFree(Worker->Region, 0, MEM_RELEASE);
    Worker->CopySource = Worker->CopyTarget = Worker->Region = NULL;
}

static const struct
{
    const char *Name;
    UNIT Unit;
    WORKFN Work;
    ULONG Start;
} SmpTests[] =
{
    { "smp_event_pingpong", UNIT_OPS, SmpEventPingPong, 256 },
    { "smp_thread_create", UNIT_OPS, SmpThreadCreate, 16 },
    { "smp_critsec_shared", UNIT_OPS, SmpCriticalSection, 4096 },
    { "smp_srw_shared", UNIT_OPS, SmpSrwShared, 4096 },
    { "smp_srw_exclusive", UNIT_OPS, SmpSrwExclusive, 4096 },
    { "smp_interlocked_shared", UNIT_OPS, SmpInterlocked, 4096 },
    { "smp_heap_alloc_free", UNIT_OPS, SmpHeap, 64 },
    { "smp_memcpy_8m", UNIT_MBPS, SmpMemcpy, 1 },
    { "smp_switch_to_thread", UNIT_OPS, SmpSwitchToThread, 1024 },
    { "smp_syscall_setevent", UNIT_OPS, SmpSyscall, 1024 },
    { "smp_shared_section", UNIT_PAGES, SmpSharedSection, 1 },
    { "smp_virtual_protect", UNIT_OPS, SmpVirtualProtect, 256 },
};

static int RunSmpSuite(WORKER *Workers, const char *Only)
{
    ULONG i;
    int Result = 0;

    InitializeCriticalSection(&SharedLock);
    for (i = 0; i < ThreadCount; i++)
    {
        if (!SmpOpen(&Workers[i]))
        {
            printf("MMCC_ERROR smp setup %lu\n", Workers[i].Status);
            Result = 1;
            break;
        }
    }

    if (Result == 0)
    {
        for (i = 0; i < sizeof(SmpTests) / sizeof(SmpTests[0]); i++)
        {
            if (TestSelected(SmpTests[i].Name, Only))
                Measure(SmpTests[i].Name, SmpTests[i].Unit, Workers, SmpTests[i].Work, SmpTests[i].Start);
        }
    }

    for (i = 0; i < ThreadCount; i++)
        SmpClose(&Workers[i]);
    DeleteCriticalSection(&SharedLock);
    return Result;
}

static BOOL ResolveBenchDir(const char *Spec, char *Path)
{
    char Root[4] = "A:\\";
    char Label[MAX_PATH + 1];
    SIZE_T Length;

    if (_strnicmp(Spec, "label:", 6) == 0)
    {
        DWORD Drives = GetLogicalDrives();
        ULONG Drive;

        for (Drive = 0; Drive < 26; Drive++)
        {
            if (!(Drives & (1u << Drive)))
                continue;
            Root[0] = (char)('A' + Drive);
            if (GetVolumeInformationA(Root, Label, sizeof(Label), NULL, NULL, NULL, NULL, 0) &&
                _stricmp(Label, Spec + 6) == 0)
            {
                break;
            }
        }
        if (Drive == 26)
            return FALSE;
        sprintf(Path, "%smmccbench\\", Root);
    }
    else
    {
        Length = strlen(Spec);
        if (Length == 0 || Length > MAX_PATH - 16)
            return FALSE;
        strcpy(Path, Spec);
        if (Path[Length - 1] != '\\')
            strcat(Path, "\\");
    }
    return CreateDirectoryA(Path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int RunClassicSuite(WORKER *Workers, const char *Only)
{
    ULONG i;

    if (Only == NULL || strcmp(Only, "mm") == 0)
        Measure("mm_fault_demand_zero", UNIT_PAGES, Workers, MmFaultDemandZero, 1);
    if (Only == NULL || strcmp(Only, "mm") == 0 || strcmp(Only, "commit") == 0)
        Measure("mm_commit_decommit", UNIT_OPS, Workers, MmCommitDecommit, 64);
    if (Only == NULL || strcmp(Only, "mm") == 0)
        Measure("mm_reserve_release", UNIT_OPS, Workers, MmReserveRelease, 64);

    for (i = 0; i < ThreadCount; i++)
    {
        if (!WorkerFileOpen(&Workers[i], TRUE))
        {
            printf("MMCC_ERROR file setup %lu\n", Workers[i].Status);
            return 1;
        }
    }

    if (Only == NULL || strcmp(Only, "mm") == 0)
        Measure("mm_section_map_unmap", UNIT_OPS, Workers, MmSectionMapUnmap, 16);
    if (Only == NULL || strcmp(Only, "cc") == 0)
    {
        Measure("cc_write_cached", UNIT_MBPS, Workers, CcWriteCached, 1);
        Measure("cc_read_hot", UNIT_MBPS, Workers, CcReadHot, 1);
        Measure("cc_read_random_hot", UNIT_OPS, Workers, CcReadRandomHot, 256);
        Measure("cc_mapped_read", UNIT_PAGES, Workers, CcMappedRead, 1);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    WORKER *Workers;
    SYSTEM_INFO SystemInfo;
    char TempPath[MAX_PATH];
    ULONG i, j;
    int Argument;
    int Result;
    const char *Only = NULL;
    const char *Suite = NULL;
    const char *Dir = NULL;
    char FsName[MAX_PATH + 1];
    char Root[4];

    if (argc > 1 && strcmp(argv[1], "-noop") == 0)
        return 0;

    for (Argument = 1; Argument < argc; Argument++)
    {
        if (strcmp(argv[Argument], "-t") == 0 && Argument + 1 < argc)
            ThreadCount = strtoul(argv[++Argument], NULL, 0);
        else if (strcmp(argv[Argument], "-f") == 0 && Argument + 1 < argc)
            FileBytes = strtoul(argv[++Argument], NULL, 0) * 1024u * 1024u;
        else if (strcmp(argv[Argument], "-a") == 0 && Argument + 1 < argc)
            ArenaBytes = strtoul(argv[++Argument], NULL, 0) * 1024u * 1024u;
        else if (strcmp(argv[Argument], "-ms") == 0 && Argument + 1 < argc)
            TargetMs = strtoul(argv[++Argument], NULL, 0);
        else if (strcmp(argv[Argument], "-only") == 0 && Argument + 1 < argc)
            Only = argv[++Argument];
        else if (strcmp(argv[Argument], "-suite") == 0 && Argument + 1 < argc)
            Suite = argv[++Argument];
        else if (strcmp(argv[Argument], "-dir") == 0 && Argument + 1 < argc)
            Dir = argv[++Argument];
        else if (strcmp(argv[Argument], "-profile") == 0)
            Profile = TRUE;
        else
        {
            printf("usage: mmccbench [-t threads] [-f fileMB] [-a arenaMB] [-ms targetMs] [-only cc|mm|commit] "
                   "[-suite general|storage|smp] [-dir path|label:NAME] [-profile]\n");
            return 1;
        }
    }

    if (Suite == NULL && Only != NULL && strcmp(Only, "cc") != 0 && strcmp(Only, "mm") != 0 && strcmp(Only, "commit") != 0)
    {
        printf("MMCC_ERROR invalid benchmark group %s\n", Only);
        return 1;
    }

    if (Suite != NULL && strcmp(Suite, "general") != 0 && strcmp(Suite, "storage") != 0 && strcmp(Suite, "smp") != 0)
    {
        printf("MMCC_ERROR invalid suite %s\n", Suite);
        return 1;
    }

    if (GetModuleFileNameA(NULL, SelfPath, sizeof(SelfPath)) == 0)
        SelfPath[0] = '\0';

    if (ThreadCount == 0 || ThreadCount > MAX_THREADS)
        ThreadCount = 1;

    GetSystemInfo(&SystemInfo);
    PageSize = SystemInfo.dwPageSize ? SystemInfo.dwPageSize : 4096;
    QueryPerformanceFrequency(&Frequency);
    if (Frequency.QuadPart == 0)
    {
        printf("MMCC_ERROR no performance counter\n");
        return 1;
    }

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (Dir != NULL)
    {
        if (!ResolveBenchDir(Dir, TempPath))
        {
            printf("MMCC_ERROR no directory for %s (%lu)\n", Dir, GetLastError());
            return 1;
        }
    }
    else if (GetTempPathA(sizeof(TempPath), TempPath) == 0)
        strcpy(TempPath, ".\\");

    Workers = (WORKER *)calloc(ThreadCount, sizeof(WORKER));
    if (Workers == NULL)
    {
        printf("MMCC_ERROR out of memory\n");
        return 1;
    }

    for (i = 0; i < ThreadCount; i++)
    {
        Workers[i].Index = i;
        Workers[i].ArenaBytes = ArenaBytes / ThreadCount;
        if (Workers[i].ArenaBytes < 1024u * 1024u)
            Workers[i].ArenaBytes = 1024u * 1024u;
        Workers[i].BufferBytes = 64u * 1024u;
        Workers[i].Buffer = (PUCHAR)VirtualAlloc(NULL, Workers[i].BufferBytes,
                                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Workers[i].Buffer == NULL)
        {
            printf("MMCC_ERROR buffer alloc %lu\n", GetLastError());
            return 1;
        }
        memset(Workers[i].Buffer, (int)(i + 1), Workers[i].BufferBytes);
        sprintf(Workers[i].Path, "%smmcc%lu.tmp", TempPath, (unsigned long)i);
        for (j = 0; j < RANDOM_SLOTS; j++)
        {
            Workers[i].Offsets[j] = (ULONG)(((j * 2654435761u) + i) % (FileBytes / RANDOM_BLOCK)) * RANDOM_BLOCK;
        }
    }

    printf("MMCC_BEGIN cpus=%lu threads=%lu page=%lu arenaMB=%lu fileMB=%lu targetMs=%lu\n",
           (unsigned long)SystemInfo.dwNumberOfProcessors, (unsigned long)ThreadCount,
           (unsigned long)PageSize, (unsigned long)(ArenaBytes / 1024u / 1024u),
           (unsigned long)(FileBytes / 1024u / 1024u), (unsigned long)TargetMs);

    if (Only != NULL)
        printf("MMCC_FILTER group=%s\n", Only);
    if (Suite != NULL)
        printf("MMCC_SUITE %s\n", Suite);
    Root[0] = TempPath[0];
    Root[1] = ':';
    Root[2] = '\\';
    Root[3] = '\0';
    if (TempPath[1] != ':' || !GetVolumeInformationA(Root, NULL, 0, NULL, NULL, NULL, FsName, sizeof(FsName)))
        strcpy(FsName, "unknown");
    printf("MMCC_DIR %s fs=%s\n", TempPath, FsName);
    fflush(stdout);

    if (Suite == NULL)
        Result = RunClassicSuite(Workers, Only);
    else if (strcmp(Suite, "storage") == 0)
        Result = RunStorageSuite(Workers, TempPath, Only);
    else if (strcmp(Suite, "smp") == 0)
        Result = RunSmpSuite(Workers, Only);
    else
        Result = RunGeneralSuite(Workers, TempPath, Only);

    for (i = 0; i < ThreadCount; i++)
    {
        WorkerFileClose(&Workers[i]);
        if (Workers[i].Buffer != NULL)
            VirtualFree(Workers[i].Buffer, 0, MEM_RELEASE);
    }
    free(Workers);

    printf("MMCC_DONE sink=%lu\n", (unsigned long)Sink);
    return Result;
}
