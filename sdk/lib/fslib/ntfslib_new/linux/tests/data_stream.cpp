/*
 * PROJECT:     LiberNT NTFS library tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File data streamed through a mounted NTFS volume, checked byte for byte
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

static const size_t Mega = 1024 * 1024;
static int Failures;

static uint64_t
Mix(uint64_t Value)
{
    Value += UINT64_C(0x9E3779B97F4A7C15);
    Value = (Value ^ (Value >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    Value = (Value ^ (Value >> 27)) * UINT64_C(0x94D049BB133111EB);
    return Value ^ (Value >> 31);
}

/* Byte Offset of the stream named by Seed. */
static void
Fill(uint64_t Seed, uint64_t Offset, unsigned char* Buffer, size_t Length)
{
    for (size_t Index = 0; Index < Length; Index++)
    {
        uint64_t Position = Offset + Index;
        uint64_t Word = Mix(Seed * UINT64_C(0x100000001B3) + Position / 8);

        Buffer[Index] = (unsigned char)(Word >> (Position % 8 * 8));
    }
}

static void
Fail(const char* Name, const char* What, uint64_t Offset)
{
    fprintf(stderr, "FAIL %s: %s at %" PRIu64 " (%s)\n", Name, What, Offset, strerror(errno));
    Failures++;
}

static bool
WriteAt(int File, uint64_t Seed, uint64_t Offset, size_t Length)
{
    std::vector<unsigned char> Buffer(Length);
    size_t Done = 0;

    Fill(Seed, Offset, Buffer.data(), Length);
    while (Done < Length)
    {
        ssize_t Result = pwrite(File, Buffer.data() + Done, Length - Done, (off_t)(Offset + Done));

        if (Result <= 0)
            return false;
        Done += (size_t)Result;
    }
    return true;
}

/* One expected piece of a file: Seed 0 reads as zero. */
struct Piece
{
    uint64_t Start;
    uint64_t End;
    uint64_t Seed;
};

static void
Verify(const std::string& Path, const char* Name, uint64_t Size, std::vector<Piece> Pieces, int Flags = 0)
{
    int File = open(Path.c_str(), O_RDONLY | Flags);
    struct stat Stat;
    void* Raw = NULL;
    const size_t Chunk = 256 * 1024;

    if (File < 0 || fstat(File, &Stat) != 0 || posix_memalign(&Raw, 4096, 2 * Chunk) != 0)
    {
        Fail(Name, "open", 0);
        if (File >= 0)
            close(File);
        return;
    }
    unsigned char* Buffer = (unsigned char*)Raw;
    unsigned char* Expected = Buffer + Chunk;

    if ((uint64_t)Stat.st_size != Size)
    {
        errno = 0;
        Fail(Name, "size", (uint64_t)Stat.st_size);
    }
    for (uint64_t Offset = 0; Offset < Size && Failures < 20; Offset += Chunk)
    {
        size_t Length = (size_t)std::min<uint64_t>(Chunk, Size - Offset);
        size_t Done = 0;

        while (Done < Length)
        {
            ssize_t Result = pread(File, Buffer + Done, (Flags & O_DIRECT) ? Chunk - Done : Length - Done, (off_t)(Offset + Done));

            if (Result <= 0)
                break;
            Done += (size_t)Result;
        }
        if (Done < Length)
        {
            Fail(Name, "short read", Offset + Done);
            break;
        }
        memset(Expected, 0, Length);
        for (const Piece& Part : Pieces)
        {
            uint64_t Start = std::max(Part.Start, Offset);
            uint64_t End = std::min(Part.End, Offset + Length);

            if (Start >= End)
                continue;
            if (Part.Seed)
                Fill(Part.Seed, Start, Expected + (Start - Offset), (size_t)(End - Start));
            else
                memset(Expected + (Start - Offset), 0, (size_t)(End - Start));
        }
        if (memcmp(Buffer, Expected, Length) != 0)
        {
            size_t Index = 0;

            while (Buffer[Index] == Expected[Index])
                Index++;
            errno = 0;
            Fail(Name, "content", Offset + Index);
            break;
        }
    }
    free(Raw);
    close(File);
}

struct Case
{
    const char* Name;
    uint64_t Size;
    std::vector<Piece> Pieces;
};

static std::vector<Piece>
RandomPieces(uint64_t Size, uint64_t Base, uint64_t Over, unsigned Count)
{
    std::vector<Piece> Pieces{{0, Size, Base}};

    for (unsigned Index = 0; Index < Count; Index++)
    {
        uint64_t Page = Mix(Over + Index) % (Size / 4096);

        Pieces.push_back({Page * 4096, Page * 4096 + 4096, Over});
    }
    return Pieces;
}

static std::vector<Case>
Cases()
{
    return {
        {"seq", 96 * Mega, {{0, 96 * Mega, 11}}},
        {"append", 48 * Mega + 12345, {{0, 48 * Mega + 12345, 12}}},
        {"rand", 64 * Mega, RandomPieces(64 * Mega, 13, 14, 6000)},
        {"gap", 64 * Mega, {{0, 2 * Mega, 15}, {8 * Mega, 9 * Mega, 15}, {40 * Mega + 4096, 40 * Mega + 8192, 15}}},
        {"trunc", 6 * Mega + 123, {{0, 5 * Mega + 123, 16}, {5 * Mega + 123, 6 * Mega + 123, 17}}},
        {"threads", 64 * Mega, {{0, 64 * Mega, 18}}},
        {"direct", 32 * Mega, {{0, 32 * Mega, 19}}},
        {"mapped", 8 * Mega, {{0, 8 * Mega, 20}}},
        {"tail", 3 * Mega + 777, {{0, Mega, 21}, {3 * Mega, 3 * Mega + 777, 21}}},
        {"mapgap", 8 * Mega, {{5 * Mega + 100, 5 * Mega + 12445, 22}}},
        {"grown", 4 * Mega, {{0, Mega + 100, 23}, {2 * Mega + 50, 2 * Mega + 1050, 23}}},
        {"regrown", 2 * Mega, {{0, Mega + 100, 24}}},
        {"mapsmall", 300, {{0, 100, 25}, {100, 200, 26}, {200, 300, 25}}},
        {"promoted", 900, {{0x237, 0x26b, 27}}},
    };
}

static void
Write(const std::string& Root)
{
    int File;

    /* Leave non-zero bytes in the clusters the files below will take. */
    File = open((Root + "/poison").c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (File < 0)
        return Fail("poison", "create", 0);
    {
        std::vector<unsigned char> Block(Mega, 0xA5);

        for (unsigned Index = 0; Index < 200; Index++)
        {
            if (write(File, Block.data(), Block.size()) != (ssize_t)Block.size())
                Fail("poison", "write", Index);
        }
    }
    fsync(File);
    close(File);
    unlink((Root + "/poison").c_str());

    /* Emptied, regrown inside its record, then grown out of it by a size change alone. */
    File = open((Root + "/promoted").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || !WriteAt(File, 27, 0, 300) || ftruncate(File, 0) != 0 || ftruncate(File, 0x8b) != 0 ||
        !WriteAt(File, 27, 0x237, 0x34) || ftruncate(File, 900) != 0)
        Fail("promoted", "write", 0);
    close(File);

    /* A store through a mapping far past anything written: what lies before it must still read as zeros. */
    File = open((Root + "/mapgap").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || ftruncate(File, 8 * Mega) != 0)
        Fail("mapgap", "create", 0);
    else
    {
        void* Map = mmap(NULL, 8 * Mega, PROT_READ | PROT_WRITE, MAP_SHARED, File, 0);

        if (Map == MAP_FAILED)
            Fail("mapgap", "mmap", 0);
        else
        {
            Fill(22, 5 * Mega + 100, (unsigned char*)Map + 5 * Mega + 100, 12345);
            msync(Map, 8 * Mega, MS_SYNC);
            munmap(Map, 8 * Mega);
        }
    }
    close(File);

    /* A file grown by truncation and then written inside the new part. */
    File = open((Root + "/grown").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || !WriteAt(File, 23, 0, Mega + 100) || ftruncate(File, 4 * Mega) != 0 ||
        !WriteAt(File, 23, 2 * Mega + 50, 1000))
        Fail("grown", "write", 0);
    close(File);

    /* Reserved first, then filled in order: the valid data length follows the writes. */
    File = open((Root + "/seq").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || posix_fallocate(File, 0, 96 * Mega) != 0)
        Fail("seq", "create", 0);
    for (uint64_t Offset = 0; Offset < 96 * Mega; Offset += Mega)
    {
        if (!WriteAt(File, 11, Offset, Mega))
            Fail("seq", "write", Offset);
    }
    fsync(File);
    /* Read back through the daemon while the file is still open for writing. */
    posix_fadvise(File, 0, 0, POSIX_FADV_DONTNEED);
    Verify(Root + "/seq", "seq-live", 96 * Mega, {{0, 96 * Mega, 11}});
    close(File);

    File = open((Root + "/append").c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0644);
    for (uint64_t Offset = 0; Offset < 48 * Mega + 12345;)
    {
        size_t Length = (size_t)std::min<uint64_t>(256 * 1024 + 7, 48 * Mega + 12345 - Offset);
        std::vector<unsigned char> Buffer(Length);

        Fill(12, Offset, Buffer.data(), Length);
        if (File < 0 || write(File, Buffer.data(), Length) != (ssize_t)Length)
        {
            Fail("append", "write", Offset);
            break;
        }
        Offset += Length;
    }
    close(File);

    File = open((Root + "/rand").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    for (uint64_t Offset = 0; Offset < 64 * Mega; Offset += Mega)
    {
        if (File < 0 || !WriteAt(File, 13, Offset, Mega))
            Fail("rand", "write", Offset);
    }
    fsync(File);
    for (unsigned Index = 0; Index < 6000; Index++)
    {
        uint64_t Page = Mix(14 + Index) % (64 * Mega / 4096);

        if (!WriteAt(File, 14, Page * 4096, 4096))
            Fail("rand", "overwrite", Page * 4096);
    }
    close(File);

    /* Bytes nothing wrote, between and after written pieces of a reserved file, read as zero. */
    File = open((Root + "/gap").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || posix_fallocate(File, 0, 64 * Mega) != 0)
        Fail("gap", "create", 0);
    if (!WriteAt(File, 15, 0, Mega) || !WriteAt(File, 15, Mega, Mega) || !WriteAt(File, 15, 8 * Mega, Mega) ||
        !WriteAt(File, 15, 40 * Mega + 4096, 4096))
    {
        Fail("gap", "write", 0);
    }
    close(File);

    File = open((Root + "/trunc").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    for (uint64_t Offset = 0; Offset < 16 * Mega; Offset += Mega)
    {
        if (File < 0 || !WriteAt(File, 16, Offset, Mega))
            Fail("trunc", "write", Offset);
    }
    if (ftruncate(File, 5 * Mega + 123) != 0 || !WriteAt(File, 17, 5 * Mega + 123, Mega))
        Fail("trunc", "truncate", 0);
    close(File);

    /* Stripes written out of order by several threads into one reserved file. */
    File = open((Root + "/threads").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || posix_fallocate(File, 0, 64 * Mega) != 0)
        Fail("threads", "create", 0);
    {
        std::vector<std::thread> Threads;

        for (unsigned Thread = 0; Thread < 4; Thread++)
        {
            Threads.emplace_back([File, Thread] {
                for (unsigned Stripe = 0; Stripe < 16; Stripe++)
                {
                    uint64_t Offset = ((uint64_t)(15 - Stripe) * 4 + Thread) * Mega;

                    if (!WriteAt(File, 18, Offset, Mega))
                        Fail("threads", "write", Offset);
                }
            });
        }
        for (std::thread& Thread : Threads)
            Thread.join();
    }
    close(File);

    /* The same through direct I/O, which reaches the daemon one request per write. */
    File = open((Root + "/direct").c_str(), O_CREAT | O_TRUNC | O_RDWR | O_DIRECT, 0644);
    if (File < 0)
        File = open((Root + "/direct").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    {
        std::vector<std::thread> Threads;

        if (File < 0 || ftruncate(File, 32 * Mega) != 0)
            Fail("direct", "create", 0);
        for (unsigned Thread = 0; Thread < 4; Thread++)
        {
            Threads.emplace_back([File, Thread] {
                void* Raw = NULL;

                if (posix_memalign(&Raw, 4096, 512 * 1024) != 0)
                    return;
                for (unsigned Stripe = 0; Stripe < 16; Stripe++)
                {
                    uint64_t Offset = ((uint64_t)Stripe * 4 + Thread) * 512 * 1024;

                    Fill(19, Offset, (unsigned char*)Raw, 512 * 1024);
                    if (pwrite(File, Raw, 512 * 1024, (off_t)Offset) != 512 * 1024)
                        Fail("direct", "write", Offset);
                }
                free(Raw);
            });
        }
        for (std::thread& Thread : Threads)
            Thread.join();
    }
    close(File);

    File = open((Root + "/mapped").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || ftruncate(File, 8 * Mega) != 0)
        Fail("mapped", "create", 0);
    else
    {
        void* Map = mmap(NULL, 8 * Mega, PROT_READ | PROT_WRITE, MAP_SHARED, File, 0);

        if (Map == MAP_FAILED)
            Fail("mapped", "mmap", 0);
        else
        {
            Fill(20, 0, (unsigned char*)Map, 8 * Mega);
            msync(Map, 8 * Mega, MS_SYNC);
            munmap(Map, 8 * Mega);
        }
    }
    close(File);

    /* Cut inside a block and grown again without a write: what was cut off must not come back. */
    File = open((Root + "/regrown").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || !WriteAt(File, 24, 0, 2 * Mega) || fsync(File) != 0 || ftruncate(File, Mega + 100) != 0 ||
        ftruncate(File, 2 * Mega) != 0)
        Fail("regrown", "write", 0);
    close(File);

    /* A file small enough to live in its record, changed through a mapping. */
    File = open((Root + "/mapsmall").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || !WriteAt(File, 25, 0, 300) || fsync(File) != 0)
        Fail("mapsmall", "write", 0);
    else
    {
        void* Map = mmap(NULL, 300, PROT_READ | PROT_WRITE, MAP_SHARED, File, 0);

        if (Map == MAP_FAILED)
            Fail("mapsmall", "mmap", 0);
        else
        {
            Fill(26, 100, (unsigned char*)Map + 100, 100);
            if (msync(Map, 300, MS_SYNC) != 0)
                Fail("mapsmall", "msync", 0);
            munmap(Map, 300);
        }
    }
    close(File);

    /* A write far past the end of a small file leaves a hole behind it. */
    File = open((Root + "/tail").c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (File < 0 || !WriteAt(File, 21, 0, Mega) || !WriteAt(File, 21, 3 * Mega, 777))
        Fail("tail", "write", 0);
    close(File);
}

static const unsigned SmallCount = 900;

static uint64_t
SmallSize(unsigned Index, uint64_t Seed)
{
    return Index % 13 == 0 ? 0 : Mix(Seed * 977 + Index) % (Index % 7 == 0 ? 200000 : 30000);
}

static std::string
SmallName(const std::string& Root, unsigned Index)
{
    char Name[64];

    snprintf(Name, sizeof(Name), "/small/f%04u", Index);
    return Root + Name;
}

static void
WriteSmall(const std::string& Root, unsigned Index, uint64_t Seed)
{
    std::string Path = SmallName(Root, Index);
    uint64_t Size = SmallSize(Index, Seed);
    int File = open(Path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);

    if (File < 0 || (Size && !WriteAt(File, Seed + Index, 0, (size_t)Size)))
        Fail(Path.c_str(), "write", 0);
    if (File >= 0)
        close(File);
}

static void
VerifySmall(const std::string& Root, unsigned Index, uint64_t Seed)
{
    uint64_t Size = SmallSize(Index, Seed);

    Verify(SmallName(Root, Index), "small", Size, {{0, Size, Seed + Index}});
}

/* Many small files, later read in listing order as an archiver would. */
static void
WriteSmallFiles(const std::string& Root)
{
    mkdir((Root + "/small").c_str(), 0755);
    for (unsigned Index = 0; Index < SmallCount; Index++)
        WriteSmall(Root, Index, 100);
}

/*
 * Reads the files in order on a fresh mount while rewriting, truncating and replacing files a few
 * places ahead: whatever the driver read ahead of the walk must not survive those changes.
 */
static void
Churn(const std::string& Root)
{
    std::vector<uint64_t> Seeds(SmallCount, 100);
    std::vector<int64_t> Cut(SmallCount, -1);

    /* A listing with attributes first, as ls -l or an archiver does. */
    for (unsigned Index = 0; Index < SmallCount; Index++)
    {
        struct stat Stat;

        if (stat(SmallName(Root, Index).c_str(), &Stat) != 0)
            Fail("small", "stat", Index);
    }
    /* A first read after a change moves the access time, also when the data came ahead of the read. */
    {
        const struct timespec Touch[2] = {{0, UTIME_OMIT}, {0, UTIME_NOW}};
        struct stat Before, After;
        std::string Path = SmallName(Root, 1);

        if (utimensat(AT_FDCWD, Path.c_str(), Touch, 0) != 0 || stat(Path.c_str(), &Before) != 0)
            Fail("small", "touch", 1);
        VerifySmall(Root, 1, 100);
        if (stat(Path.c_str(), &After) != 0 ||
            (After.st_atim.tv_sec == Before.st_atim.tv_sec && After.st_atim.tv_nsec == Before.st_atim.tv_nsec))
        {
            errno = 0;
            Fail("small", "access time not moved by the first read", 1);
        }
    }
    for (unsigned Index = 0; Index < SmallCount; Index++)
    {
        if (Index % 9 == 0 && Index + 5 < SmallCount)
        {
            Seeds[Index + 5] = 200;
            WriteSmall(Root, Index + 5, 200);
        }
        /* The first half keeps the names in place, so the walk stays read ahead; the second replaces files too. */
        if (Index >= SmallCount / 2 && Index % 11 == 0 && Index + 9 < SmallCount)
        {
            unlink(SmallName(Root, Index + 9).c_str());
            Seeds[Index + 9] = 300;
            Cut[Index + 9] = -1;
            WriteSmall(Root, Index + 9, 300);
        }
        if (Index % 17 == 0 && Index + 3 < SmallCount && SmallSize(Index + 3, Seeds[Index + 3]) > 100)
        {
            Cut[Index + 3] = 100;
            if (truncate(SmallName(Root, Index + 3).c_str(), 100) != 0)
                Fail("small", "truncate", Index + 3);
        }
        if (Cut[Index] >= 0)
            Verify(SmallName(Root, Index), "small-cut", (uint64_t)Cut[Index], {{0, (uint64_t)Cut[Index], Seeds[Index] + Index}});
        else
            VerifySmall(Root, Index, Seeds[Index]);
    }
    /* And once more, now from whatever the kernel kept. */
    for (unsigned Index = 0; Index < SmallCount; Index++)
    {
        if (Cut[Index] >= 0)
            Verify(SmallName(Root, Index), "small-cut", (uint64_t)Cut[Index], {{0, (uint64_t)Cut[Index], Seeds[Index] + Index}});
        else
            VerifySmall(Root, Index, Seeds[Index]);
    }
}

int
main(int Argc, char** Argv)
{
    if (Argc != 3 || (strcmp(Argv[1], "write") != 0 && strcmp(Argv[1], "verify") != 0 && strcmp(Argv[1], "churn") != 0))
    {
        fprintf(stderr, "usage: %s write|verify|churn DIRECTORY\n", Argv[0]);
        return 2;
    }
    std::string Root = Argv[2];

    if (strcmp(Argv[1], "churn") == 0)
    {
        Churn(Root);
        if (Failures)
            return 1;
        printf("small files read ahead and changed under the walk: %u files\n", SmallCount);
        return 0;
    }
    if (strcmp(Argv[1], "write") == 0)
        WriteSmallFiles(Root);
    for (unsigned Index = 0; Index < SmallCount; Index++)
        VerifySmall(Root, Index, 100);

    if (strcmp(Argv[1], "write") == 0)
        Write(Root);
    for (const Case& Each : Cases())
    {
        Verify(Root + "/" + Each.Name, Each.Name, Each.Size, Each.Pieces);
        /* Past the page cache, where the driver itself answers. */
        if (strcmp(Argv[1], "write") == 0)
            Verify(Root + "/" + Each.Name, Each.Name, Each.Size, Each.Pieces, O_DIRECT);
    }
    if (Failures)
        return 1;
    printf("data streams %s: %zu files\n", strcmp(Argv[1], "write") == 0 ? "written and read back" : "verified", Cases().size());
    return 0;
}
