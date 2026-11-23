/*
 * PROJECT:     LiberNT NTFS library tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POSIX edge cases run against a mounted NTFS volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/aio_abi.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0)
#endif
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE 0x01
#endif

static unsigned Failures;
static bool CaseInsensitive;

static void
Report(const char* Kind, const char* Name, const char* Format, ...)
{
    va_list Arguments;

    printf("%s %s", Kind, Name);
    if (Format && *Format)
    {
        printf(": ");
        va_start(Arguments, Format);
        vprintf(Format, Arguments);
        va_end(Arguments);
    }
    printf("\n");
    fflush(stdout);
}

#define PASS(Name) Report("PASS", Name, "")
#define INFO(Name, ...) Report("INFO", Name, __VA_ARGS__)
#define FAIL(Name, ...) (Failures++, Report("FAIL", Name, __VA_ARGS__))
#define CHECK(Name, Condition, ...) \
    do { if (Condition) PASS(Name); else FAIL(Name, __VA_ARGS__); } while (0)

static std::string Root;

static std::string
P(const std::string& Relative)
{
    return Root + "/" + Relative;
}

static uint8_t
PatternByte(uint32_t Seed, uint64_t Offset)
{
    uint64_t Value = (Offset + 1) * UINT64_C(0x9E3779B97F4A7C15) + Seed;

    Value ^= Value >> 29;
    return (uint8_t)(Value >> 17);
}

static std::vector<uint8_t>
Pattern(uint32_t Seed, uint64_t Offset, size_t Length)
{
    std::vector<uint8_t> Data(Length);

    for (size_t Index = 0; Index < Length; Index++)
        Data[Index] = PatternByte(Seed, Offset + Index);
    return Data;
}

static bool
WriteAll(int Fd, const void* Buffer, size_t Length, off_t Offset)
{
    const uint8_t* Bytes = (const uint8_t*)Buffer;

    while (Length != 0)
    {
        ssize_t Done = pwrite(Fd, Bytes, Length, Offset);

        if (Done <= 0)
            return false;
        Bytes += Done;
        Length -= (size_t)Done;
        Offset += Done;
    }
    return true;
}

static ssize_t
ReadAll(int Fd, void* Buffer, size_t Length, off_t Offset)
{
    uint8_t* Bytes = (uint8_t*)Buffer;
    size_t Total = 0;

    while (Total < Length)
    {
        ssize_t Done = pread(Fd, Bytes + Total, Length - Total, Offset + (off_t)Total);

        if (Done < 0)
            return -1;
        if (Done == 0)
            break;
        Total += (size_t)Done;
    }
    return (ssize_t)Total;
}

static bool
WriteFile(const std::string& Path, const std::vector<uint8_t>& Data)
{
    int Fd = open(Path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool Ok;

    if (Fd < 0)
        return false;
    Ok = WriteAll(Fd, Data.data(), Data.size(), 0);
    return close(Fd) == 0 && Ok;
}

static bool
ReadFile(const std::string& Path, std::vector<uint8_t>& Data)
{
    struct stat Stat;
    int Fd = open(Path.c_str(), O_RDONLY);
    ssize_t Done;

    Data.clear();
    if (Fd < 0)
        return false;
    if (fstat(Fd, &Stat) != 0)
    {
        close(Fd);
        return false;
    }
    Data.resize((size_t)Stat.st_size);
    Done = ReadAll(Fd, Data.data(), Data.size(), 0);
    close(Fd);
    return Done == (ssize_t)Data.size();
}

static bool
Exists(const std::string& Path)
{
    struct stat Stat;

    return lstat(Path.c_str(), &Stat) == 0;
}

static off_t
SizeOf(const std::string& Path)
{
    struct stat Stat;

    return stat(Path.c_str(), &Stat) == 0 ? Stat.st_size : -1;
}

static std::set<std::string>
ListNames(const std::string& Path)
{
    std::set<std::string> Names;
    DIR* Directory = opendir(Path.c_str());
    struct dirent* Entry;

    if (!Directory)
        return Names;
    while ((Entry = readdir(Directory)) != NULL)
    {
        if (strcmp(Entry->d_name, ".") && strcmp(Entry->d_name, ".."))
            Names.insert(Entry->d_name);
    }
    closedir(Directory);
    return Names;
}

static int
ErrnoOf(int Result)
{
    return Result < 0 ? errno : 0;
}

static void
ExpectErrno(const char* Name, int Result, int Expected)
{
    int Actual = ErrnoOf(Result);

    if (Actual == Expected)
        PASS(Name);
    else
        FAIL(Name, "expected %s, got %s", strerrorname_np(Expected) ? strerrorname_np(Expected) : "0",
             Actual ? strerrorname_np(Actual) : "success");
}

static void
MakeDir(const std::string& Relative)
{
    if (mkdir(P(Relative).c_str(), 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "mkdir %s: %s\n", Relative.c_str(), strerror(errno));
        exit(2);
    }
}

static void
TestDataSizes()
{
    static const size_t Sizes[] = {0, 1, 600, 700, 900, 1023, 4095, 4096, 4097,
                                   65535, 65536, 1048576 + 3};
    bool Ok = true;
    char Detail[128] = "";

    MakeDir("sizes");
    for (size_t Index = 0; Index < sizeof(Sizes) / sizeof(Sizes[0]); Index++)
    {
        std::string Path = P("sizes/f" + std::to_string(Sizes[Index]));
        std::vector<uint8_t> Expected = Pattern((uint32_t)Index, 0, Sizes[Index]);
        std::vector<uint8_t> Actual;

        if (!WriteFile(Path, Expected) || !ReadFile(Path, Actual) || Actual != Expected)
        {
            snprintf(Detail, sizeof(Detail), "size %zu mismatch", Sizes[Index]);
            Ok = false;
            break;
        }
    }
    CHECK("data.sizes", Ok, "%s", Detail);
}

static void
TestAllocationTrim()
{
    static const size_t Sizes[] = {8942, 70000, 1048576 + 5, 3 * 1048576 + 7};
    char Name[64];

    MakeDir("trim");
    for (size_t Size : Sizes)
    {
        std::string Path = P("trim/f" + std::to_string(Size));
        std::vector<uint8_t> Data = Pattern(13, 0, Size);
        std::vector<uint8_t> Actual;
        struct stat Stat;
        int Fd = open(Path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        size_t First = 4608;
        bool Ok = Fd >= 0 && WriteAll(Fd, Data.data(), First, 0);

        for (size_t Offset = First; Ok && Offset < Size; Offset += 10240)
            Ok = WriteAll(Fd, Data.data() + Offset, std::min<size_t>(10240, Size - Offset),
                          (off_t)Offset);
        if (Fd >= 0)
            close(Fd);
        Ok = Ok && stat(Path.c_str(), &Stat) == 0 && ReadFile(Path, Actual) && Actual == Data;
        snprintf(Name, sizeof(Name), "alloc.trim_after_close.%zu", Size);
        CHECK(Name, Ok && (uint64_t)Stat.st_blocks * 512 <= ((Size + 65535) & ~(size_t)65535),
              "size %zu allocated %llu bytes", Size,
              (unsigned long long)Stat.st_blocks * 512);
    }
}

static void
TestEofReads()
{
    std::string Path = P("eof.bin");
    std::vector<uint8_t> Data = Pattern(7, 0, 5000);
    uint8_t Buffer[8192];
    int Fd;

    WriteFile(Path, Data);
    Fd = open(Path.c_str(), O_RDONLY);
    CHECK("read.past_eof", pread(Fd, Buffer, 100, 6000) == 0, "nonzero read past EOF");
    CHECK("read.span_eof", pread(Fd, Buffer, 8192, 4000) == 1000 &&
                               !memcmp(Buffer, Data.data() + 4000, 1000),
          "short read across EOF wrong");
    CHECK("read.at_eof", pread(Fd, Buffer, 1, 5000) == 0, "read at EOF not 0");
    close(Fd);
}

static void
TestTruncateZeroes()
{
    static const struct { size_t Write, Shrink, Grow; } Cases[] = {
        {500, 100, 500},
        {65536, 1000, 65536},
        {65536, 4096, 20000},
        {300000, 4097, 300000},
        {8192, 0, 8192},
    };
    char Name[64];

    for (size_t Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); Index++)
    {
        std::string Path = P("trunc" + std::to_string(Index));
        std::vector<uint8_t> Data(Cases[Index].Write, 0xAA);
        std::vector<uint8_t> Actual;
        bool Ok;

        WriteFile(Path, Data);
        Ok = truncate(Path.c_str(), (off_t)Cases[Index].Shrink) == 0 &&
             truncate(Path.c_str(), (off_t)Cases[Index].Grow) == 0 &&
             ReadFile(Path, Actual) && Actual.size() == Cases[Index].Grow;
        for (size_t Byte = 0; Ok && Byte < Actual.size(); Byte++)
        {
            if (Actual[Byte] != (Byte < Cases[Index].Shrink ? 0xAA : 0))
                Ok = false;
        }
        snprintf(Name, sizeof(Name), "truncate.zero_tail.%zu_%zu_%zu",
                 Cases[Index].Write, Cases[Index].Shrink, Cases[Index].Grow);
        CHECK(Name, Ok, "stale data after shrink then grow");
    }

    {
        std::string Path = P("trunc-open");
        std::vector<uint8_t> Data(40000, 0x55);
        uint8_t Buffer[16];
        int Fd;

        WriteFile(Path, Data);
        Fd = open(Path.c_str(), O_RDWR);
        bool Ok = ftruncate(Fd, 10) == 0 && ftruncate(Fd, 40000) == 0 &&
                  pread(Fd, Buffer, 16, 39984) == 16;
        for (int Index = 0; Ok && Index < 16; Index++)
            Ok = Buffer[Index] == 0;
        close(Fd);
        CHECK("truncate.ftruncate_regrow", Ok, "stale tail through ftruncate");
    }
}

static void
TestHoleWrite()
{
    std::string Path = P("hole.bin");
    const off_t Far = 10 * 1024 * 1024;
    uint8_t Buffer[4096];
    struct stat Stat;
    int Fd = open(Path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    bool Ok;

    Ok = Fd >= 0 && pwrite(Fd, "Z", 1, Far) == 1 && fstat(Fd, &Stat) == 0 &&
         Stat.st_size == Far + 1;
    CHECK("write.far_offset_size", Ok, "size %lld", Ok ? 0LL : (long long)Stat.st_size);
    Ok = pread(Fd, Buffer, sizeof(Buffer), Far / 2) == (ssize_t)sizeof(Buffer);
    for (size_t Index = 0; Ok && Index < sizeof(Buffer); Index++)
        Ok = Buffer[Index] == 0;
    CHECK("write.far_offset_gap_zero", Ok, "gap not zero");
    INFO("write.far_offset_blocks", "st_blocks=%lld for size %lld",
         (long long)Stat.st_blocks, (long long)Stat.st_size);
    close(Fd);
}

static void
TestAppend()
{
    std::string Path = P("append.txt");
    std::vector<uint8_t> Actual;
    int Fd;

    WriteFile(Path, std::vector<uint8_t>{'a', 'b'});
    Fd = open(Path.c_str(), O_WRONLY | O_APPEND);
    bool Ok = Fd >= 0 && write(Fd, "cd", 2) == 2 && lseek(Fd, 0, SEEK_SET) == 0 &&
              write(Fd, "ef", 2) == 2;
    close(Fd);
    Ok = Ok && ReadFile(Path, Actual) &&
         std::string(Actual.begin(), Actual.end()) == "abcdef";
    CHECK("write.o_append", Ok, "got '%s'", std::string(Actual.begin(), Actual.end()).c_str());
}

static void
TestRandomOps()
{
    std::string Path = P("fsx.bin");
    std::vector<uint8_t> Model;
    uint32_t State = 0x1234567;
    const size_t MaxSize = 3 * 1024 * 1024;
    int Fd = open(Path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    char Detail[200] = "";
    bool Ok = Fd >= 0;
    auto Next = [&State]() {
        State ^= State << 13;
        State ^= State >> 17;
        State ^= State << 5;
        return State;
    };

    for (unsigned Step = 0; Ok && Step < 2500; Step++)
    {
        unsigned Operation = Next() % 10;
        size_t Offset = Next() % MaxSize;
        size_t Length = (Next() % 4 == 0) ? Next() % 200000 : Next() % 5000;

        if (Offset + Length > MaxSize)
            Length = MaxSize - Offset;
        if (Operation < 5)
        {
            std::vector<uint8_t> Data = Pattern(Step, Offset, Length);

            if (!WriteAll(Fd, Data.data(), Data.size(), (off_t)Offset))
            {
                snprintf(Detail, sizeof(Detail), "step %u write %zu+%zu: %s",
                         Step, Offset, Length, strerror(errno));
                Ok = false;
                break;
            }
            if (Model.size() < Offset + Length)
                Model.resize(Offset + Length, 0);
            std::copy(Data.begin(), Data.end(), Model.begin() + (ptrdiff_t)Offset);
        }
        else if (Operation < 7)
        {
            size_t NewSize = Next() % 2 ? Offset : Offset / 37;

            if (ftruncate(Fd, (off_t)NewSize) != 0)
            {
                snprintf(Detail, sizeof(Detail), "step %u truncate %zu: %s",
                         Step, NewSize, strerror(errno));
                Ok = false;
                break;
            }
            Model.resize(NewSize, 0);
        }
        else if (Operation < 9)
        {
            std::vector<uint8_t> Buffer(Length);
            ssize_t Done = ReadAll(Fd, Buffer.data(), Length, (off_t)Offset);
            size_t Expected = Offset >= Model.size() ? 0 : std::min(Length, Model.size() - Offset);

            if (Done != (ssize_t)Expected ||
                !std::equal(Buffer.begin(), Buffer.begin() + (ptrdiff_t)Expected,
                            Model.begin() + (ptrdiff_t)std::min(Offset, Model.size())))
            {
                snprintf(Detail, sizeof(Detail), "step %u read %zu+%zu got %zd want %zu",
                         Step, Offset, Length, Done, Expected);
                Ok = false;
                break;
            }
        }
        else
        {
            struct stat Stat;

            close(Fd);
            Fd = open(Path.c_str(), O_RDWR);
            if (Fd < 0 || fstat(Fd, &Stat) != 0 || (size_t)Stat.st_size != Model.size())
            {
                snprintf(Detail, sizeof(Detail), "step %u reopen size %lld want %zu",
                         Step, Fd < 0 ? -1LL : (long long)Stat.st_size, Model.size());
                Ok = false;
                break;
            }
        }
    }
    if (Ok)
    {
        std::vector<uint8_t> Actual;

        close(Fd);
        Fd = -1;
        Ok = ReadFile(Path, Actual) && Actual == Model;
        if (!Ok)
            snprintf(Detail, sizeof(Detail), "final content differs (size %zu want %zu)",
                     Actual.size(), Model.size());
    }
    if (Fd >= 0)
        close(Fd);
    CHECK("data.random_ops", Ok, "%s", Detail);
}

static void
TestFallocate()
{
    std::string Path = P("falloc.bin");
    struct stat Stat;
    int Fd = open(Path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    int Result;

    Result = fallocate(Fd, 0, 0, 1048576);
    fstat(Fd, &Stat);
    if (Result != 0)
        FAIL("fallocate.mode0_extends", "fallocate: %s", strerror(errno));
    else
        CHECK("fallocate.mode0_extends", Stat.st_size == 1048576,
              "size %lld after fallocate(0, 1 MiB)", (long long)Stat.st_size);
    {
        uint8_t Buffer[4096];
        bool Ok = pread(Fd, Buffer, sizeof(Buffer), 1048576 - 4096) ==
                  (Stat.st_size == 1048576 ? 4096 : 0);
        for (size_t Index = 0; Ok && Stat.st_size == 1048576 && Index < sizeof(Buffer); Index++)
            Ok = Buffer[Index] == 0;
        CHECK("fallocate.mode0_zero", Ok, "preallocated range not zero");
    }

    Result = fallocate(Fd, 0, 0, 4096);
    fstat(Fd, &Stat);
    CHECK("fallocate.mode0_never_shrinks", Result == 0 && Stat.st_size == 1048576,
          "size %lld after smaller fallocate", (long long)Stat.st_size);
    CHECK("fallocate.blocks", Stat.st_blocks * 512 >= 1048576,
          "st_blocks %lld after 1 MiB preallocation", (long long)Stat.st_blocks);

    Result = fallocate(Fd, FALLOC_FL_KEEP_SIZE, 0, 4 * 1048576);
    fstat(Fd, &Stat);
    if (Result != 0)
    {
        INFO("fallocate.keep_size", "%s", strerror(errno));
    }
    else
    {
        CHECK("fallocate.keep_size", Stat.st_size == 1048576, "size %lld",
              (long long)Stat.st_size);
        INFO("fallocate.keep_size_blocks", "st_blocks %lld after 4 MiB KEEP_SIZE",
             (long long)Stat.st_blocks);
    }
    {
        std::vector<uint8_t> Data = Pattern(3, 0, 100);
        bool Ok = WriteAll(Fd, Data.data(), Data.size(), 2 * 1048576);
        fstat(Fd, &Stat);
        CHECK("fallocate.write_after_keep_size", Ok && Stat.st_size == 2 * 1048576 + 100,
              "size %lld", (long long)Stat.st_size);
    }
    close(Fd);

    Fd = open(P("posixfalloc.bin").c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    Result = posix_fallocate(Fd, 4096, 8192);
    fstat(Fd, &Stat);
    CHECK("fallocate.posix_fallocate", Result == 0 && Stat.st_size == 12288,
          "result %d size %lld", Result, (long long)Stat.st_size);
    close(Fd);
}

static void
TestNames()
{
    std::string Long255(255, 'n');
    std::string Long256(256, 'n');
    std::string Accent;
    std::string EmojiOk;
    std::string EmojiTooLong;
    int Fd;

    MakeDir("names");
    Fd = open(P("names/" + Long255).c_str(), O_CREAT | O_WRONLY, 0644);
    CHECK("name.255", Fd >= 0, "%s", strerror(errno));
    if (Fd >= 0)
        close(Fd);
    Fd = open(P("names/" + Long256).c_str(), O_CREAT | O_WRONLY, 0644);
    ExpectErrno("name.256", Fd, ENAMETOOLONG);
    if (Fd >= 0)
        close(Fd);

    for (int Index = 0; Index < 200; Index++)
        Accent += "\xc3\xa9";
    Fd = open(P("names/" + Accent).c_str(), O_CREAT | O_WRONLY, 0644);
    CHECK("name.200_two_byte_utf8", Fd >= 0, "%s", strerror(errno));
    if (Fd >= 0)
        close(Fd);

    for (int Index = 0; Index < 127; Index++)
        EmojiOk += "\xf0\x9f\x98\x80";
    EmojiTooLong = EmojiOk + "\xf0\x9f\x98\x80";
    Fd = open(P("names/" + EmojiOk).c_str(), O_CREAT | O_WRONLY, 0644);
    CHECK("name.254_utf16_units_surrogates", Fd >= 0, "%s", strerror(errno));
    if (Fd >= 0)
        close(Fd);
    Fd = open(P("names/" + EmojiTooLong).c_str(), O_CREAT | O_WRONLY, 0644);
    ExpectErrno("name.256_utf16_units_surrogates", Fd, ENAMETOOLONG);
    if (Fd >= 0)
        close(Fd);

    {
        std::set<std::string> Names = ListNames(P("names"));
        CHECK("name.readdir_roundtrip",
              Names.count(Long255) && Names.count(Accent) && Names.count(EmojiOk) &&
                  Names.size() == 3,
              "%zu names", Names.size());
    }

    static const char* const Special[] = {"back\\slash", "col:on", "star*", "q?mark",
                                          "quote\"", "lt<", "gt>", "pipe|",
                                          "dot.", "space ", " lead", "tab\tname"};
    MakeDir("special");
    for (const char* Name : Special)
    {
        std::string Test = std::string("name.special.") + Name;
        std::vector<uint8_t> Data(Name, Name + strlen(Name));
        std::vector<uint8_t> Actual;
        bool Created = WriteFile(P(std::string("special/") + Name), Data);

        if (!Created)
        {
            INFO(Test.c_str(), "create refused: %s", strerror(errno));
            continue;
        }
        bool Ok = ReadFile(P(std::string("special/") + Name), Actual) && Actual == Data &&
                  ListNames(P("special")).count(Name) == 1;
        CHECK(Test.c_str(), Ok, "created but readdir/data differ");
    }
    {
        std::set<std::string> Names = ListNames(P("special"));
        bool Ok = true;
        for (const std::string& Name : Names)
        {
            std::vector<uint8_t> Actual;
            Ok = Ok && ReadFile(P("special/" + Name), Actual) &&
                 std::string(Actual.begin(), Actual.end()) == Name;
        }
        CHECK("name.special.no_aliasing", Ok, "a special name reads another file's data");
    }
}

static void
TestCase()
{
    std::vector<uint8_t> Data = Pattern(11, 0, 3000);
    std::vector<uint8_t> Actual;

    MakeDir("case");
    WriteFile(P("case/Mixed.txt"), Data);
    CaseInsensitive = Exists(P("case/mixed.txt"));
    INFO("case.sensitivity", "%s", CaseInsensitive ? "case-insensitive" : "case-sensitive");

    CHECK("case.rename_case_only",
          rename(P("case/Mixed.txt").c_str(), P("case/MIXED.TXT").c_str()) == 0,
          "%s", strerror(errno));
    {
        std::set<std::string> Names = ListNames(P("case"));
        CHECK("case.rename_case_only_listing",
              Names.size() == 1 && Names.count("MIXED.TXT") == 1,
              "listing has %zu names%s", Names.size(),
              Names.empty() ? " (file lost)" : "");
    }
    CHECK("case.rename_case_only_data", ReadFile(P("case/MIXED.TXT"), Actual) && Actual == Data,
          "data lost after case-only rename");

    if (!CaseInsensitive)
    {
        int Fd = open(P("case/mixed.txt").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);

        if (Fd < 0 && errno == EEXIST)
        {
            INFO("case.distinct_names", "exact-case lookups, case-insensitive uniqueness");
        }
        else
        {
            if (Fd >= 0)
                close(Fd);
            CHECK("case.distinct_names", ListNames(P("case")).size() == 2, "names collapsed");
        }
    }
    else
    {
        int Fd = open(P("case/mixed.txt").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        ExpectErrno("case.excl_create_other_case", Fd, EEXIST);
        if (Fd >= 0)
            close(Fd);
    }
}

static void
TestRename()
{
    std::vector<uint8_t> A = Pattern(21, 0, 5000);
    std::vector<uint8_t> B = Pattern(22, 0, 9000);
    std::vector<uint8_t> Actual;
    struct stat Stat;

    MakeDir("ren");
    WriteFile(P("ren/a"), A);
    WriteFile(P("ren/b"), B);
    CHECK("rename.replace_file", rename(P("ren/a").c_str(), P("ren/b").c_str()) == 0 &&
                                     !Exists(P("ren/a")) && ReadFile(P("ren/b"), Actual) &&
                                     Actual == A,
          "replace failed");

    MakeDir("ren/d1");
    MakeDir("ren/d2");
    WriteFile(P("ren/d1/inner"), B);
    CHECK("rename.dir_over_empty_dir",
          rename(P("ren/d1").c_str(), P("ren/d2").c_str()) == 0 && !Exists(P("ren/d1")) &&
              ReadFile(P("ren/d2/inner"), Actual) && Actual == B,
          "%s", strerror(errno));

    MakeDir("ren/d3");
    {
        int Result = rename(P("ren/d3").c_str(), P("ren/d2").c_str());
        int Error = ErrnoOf(Result);
        CHECK("rename.dir_over_nonempty_dir", Result != 0 && (Error == ENOTEMPTY || Error == EEXIST),
              "got %s", Result == 0 ? "success" : strerror(Error));
        CHECK("rename.dir_over_nonempty_dir_intact",
              Exists(P("ren/d3")) && ReadFile(P("ren/d2/inner"), Actual) && Actual == B,
              "trees changed");
    }

    WriteFile(P("ren/file"), A);
    MakeDir("ren/emptydir");
    ExpectErrno("rename.file_over_dir", rename(P("ren/file").c_str(), P("ren/emptydir").c_str()),
                EISDIR);
    CHECK("rename.file_over_dir_intact",
          Exists(P("ren/file")) && stat(P("ren/emptydir").c_str(), &Stat) == 0 &&
              S_ISDIR(Stat.st_mode),
          "file or directory lost");

    ExpectErrno("rename.dir_over_file", rename(P("ren/emptydir").c_str(), P("ren/file").c_str()),
                ENOTDIR);
    CHECK("rename.dir_over_file_intact",
          ReadFile(P("ren/file"), Actual) && Actual == A && Exists(P("ren/emptydir")),
          "file or directory lost");

    ExpectErrno("rename.into_own_subdir",
                rename(P("ren/d2").c_str(), P("ren/d2/inner2").c_str()), EINVAL);
    ExpectErrno("rename.missing_parent",
                rename(P("ren/file").c_str(), P("ren/nope/file").c_str()), ENOENT);
    ExpectErrno("rename.missing_source",
                rename(P("ren/nope").c_str(), P("ren/other").c_str()), ENOENT);

    WriteFile(P("ren/nr1"), A);
    WriteFile(P("ren/nr2"), B);
    ExpectErrno("rename.noreplace",
                renameat2(AT_FDCWD, P("ren/nr1").c_str(), AT_FDCWD, P("ren/nr2").c_str(),
                          RENAME_NOREPLACE),
                EEXIST);
    CHECK("rename.noreplace_intact",
          ReadFile(P("ren/nr1"), Actual) && Actual == A && ReadFile(P("ren/nr2"), Actual) &&
              Actual == B,
          "data changed");

    MakeDir("ren/src");
    MakeDir("ren/dst");
    WriteFile(P("ren/src/moved"), A);
    CHECK("rename.cross_dir",
          rename(P("ren/src/moved").c_str(), P("ren/dst/moved2").c_str()) == 0 &&
              ReadFile(P("ren/dst/moved2"), Actual) && Actual == A &&
              ListNames(P("ren/src")).empty(),
          "cross-directory move failed");

    MakeDir("ren/tree");
    MakeDir("ren/tree/sub");
    WriteFile(P("ren/tree/sub/leaf"), B);
    CHECK("rename.dir_children_follow",
          rename(P("ren/tree").c_str(), P("ren/tree2").c_str()) == 0 &&
              ReadFile(P("ren/tree2/sub/leaf"), Actual) && Actual == B &&
              !Exists(P("ren/tree/sub/leaf")),
          "children not reachable after directory rename");

    {
        int Fd = open(P("ren/dst/moved2").c_str(), O_RDWR);
        bool Ok = Fd >= 0 && rename(P("ren/dst/moved2").c_str(), P("ren/dst/moved3").c_str()) == 0 &&
                  pwrite(Fd, "XY", 2, 0) == 2;
        if (Fd >= 0)
            close(Fd);
        Ok = Ok && ReadFile(P("ren/dst/moved3"), Actual) && Actual.size() == A.size() &&
             Actual[0] == 'X' && Actual[1] == 'Y';
        CHECK("rename.open_file_keeps_handle", Ok, "write through renamed handle lost");
    }
}

static void
TestHardLinks()
{
    std::vector<uint8_t> A = Pattern(31, 0, 7000);
    std::vector<uint8_t> Actual;
    struct stat First, Second;

    MakeDir("links");
    WriteFile(P("links/one"), A);
    CHECK("link.create", link(P("links/one").c_str(), P("links/two").c_str()) == 0, "%s",
          strerror(errno));
    stat(P("links/one").c_str(), &First);
    stat(P("links/two").c_str(), &Second);
    CHECK("link.nlink", First.st_nlink == 2 && Second.st_nlink == 2, "nlink %lu/%lu",
          (unsigned long)First.st_nlink, (unsigned long)Second.st_nlink);
    CHECK("link.same_inode", First.st_ino == Second.st_ino, "st_ino %llu vs %llu",
          (unsigned long long)First.st_ino, (unsigned long long)Second.st_ino);
    {
        int Fd = open(P("links/two").c_str(), O_WRONLY);
        pwrite(Fd, "QQ", 2, 100);
        close(Fd);
        CHECK("link.shared_data", ReadFile(P("links/one"), Actual) && Actual[100] == 'Q',
              "write through one link not visible through the other");
        A[100] = A[101] = 'Q';
    }
    CHECK("link.rename_onto_same_file",
          rename(P("links/one").c_str(), P("links/two").c_str()) == 0 &&
              Exists(P("links/one")) && Exists(P("links/two")),
          "one=%d two=%d", Exists(P("links/one")), Exists(P("links/two")));
    unlink(P("links/one").c_str());
    stat(P("links/two").c_str(), &Second);
    CHECK("link.unlink_one", ReadFile(P("links/two"), Actual) && Actual == A &&
                                 Second.st_nlink == 1,
          "nlink %lu", (unsigned long)Second.st_nlink);
    MakeDir("links/dir");
    ExpectErrno("link.directory", link(P("links/dir").c_str(), P("links/dirlink").c_str()), EPERM);
    ExpectErrno("link.existing_target",
                link(P("links/two").c_str(), P("links/dir").c_str()), EEXIST);
    {
        bool Ok = true;
        for (int Index = 0; Ok && Index < 60; Index++)
            Ok = link(P("links/two").c_str(), P("links/l" + std::to_string(Index)).c_str()) == 0;
        stat(P("links/two").c_str(), &Second);
        CHECK("link.many", Ok && Second.st_nlink == 61, "nlink %lu", (unsigned long)Second.st_nlink);
    }
}

static void
TestUnlinkOpen()
{
    std::vector<uint8_t> A = Pattern(41, 0, 20000);
    std::vector<uint8_t> Buffer(A.size());
    int Fd;

    MakeDir("unl");
    WriteFile(P("unl/victim"), A);
    Fd = open(P("unl/victim").c_str(), O_RDWR);
    CHECK("unlink.open_file", unlink(P("unl/victim").c_str()) == 0, "%s", strerror(errno));
    CHECK("unlink.open_gone", !Exists(P("unl/victim")), "name still resolves");
    {
        struct stat Stat;

        CHECK("unlink.open_nlink_zero", fstat(Fd, &Stat) == 0 && Stat.st_nlink == 0,
              "nlink %lu", (unsigned long)Stat.st_nlink);
    }
    CHECK("unlink.open_read", ReadAll(Fd, Buffer.data(), Buffer.size(), 0) == (ssize_t)A.size() &&
                                  Buffer == A,
          "data unreadable through open handle");
    CHECK("unlink.open_write", pwrite(Fd, "W", 1, 30000) == 1, "%s", strerror(errno));
    close(Fd);
    {
        std::set<std::string> Names = ListNames(P("unl"));
        CHECK("unlink.open_released", Names.empty(), "%zu leftover names (%s)", Names.size(),
              Names.empty() ? "" : Names.begin()->c_str());
    }
    WriteFile(P("unl/old"), A);
    int PathFd = open(P("unl/old").c_str(), O_PATH);
    struct stat Old = {}, New = {};
    bool Held = PathFd >= 0 && fstat(PathFd, &Old) == 0 && unlink(P("unl/old").c_str()) == 0;
    WriteFile(P("unl/new"), std::vector<uint8_t>{7});
    CHECK("unlink.path_handle_identity", Held && stat(P("unl/new").c_str(), &New) == 0 &&
          New.st_ino != Old.st_ino && fstat(PathFd, &Old) == 0 && Old.st_nlink == 0 &&
          Old.st_size == (off_t)A.size(), "old inode %llu links %lu, new inode %llu",
          (unsigned long long)Old.st_ino, (unsigned long)Old.st_nlink, (unsigned long long)New.st_ino);
    if (PathFd >= 0)
        close(PathFd);
    Fd = open(P("unl/new").c_str(), O_RDONLY);
    CHECK("unlink.deferred_times_sync", Fd >= 0 && syncfs(Fd) == 0, "%s", strerror(errno));
    if (Fd >= 0)
        close(Fd);
}

static void
TestErrors()
{
    int Fd;

    MakeDir("err");
    MakeDir("err/full");
    WriteFile(P("err/full/x"), std::vector<uint8_t>{1});
    WriteFile(P("err/file"), std::vector<uint8_t>{1});
    ExpectErrno("errno.rmdir_nonempty", rmdir(P("err/full").c_str()), ENOTEMPTY);
    ExpectErrno("errno.rmdir_file", rmdir(P("err/file").c_str()), ENOTDIR);
    ExpectErrno("errno.unlink_dir", unlink(P("err/full").c_str()), EISDIR);
    ExpectErrno("errno.mkdir_existing", mkdir(P("err/full").c_str(), 0755), EEXIST);
    ExpectErrno("errno.mkdir_over_file", mkdir(P("err/file").c_str(), 0755), EEXIST);
    Fd = open(P("err/file/").c_str(), O_RDONLY);
    ExpectErrno("errno.trailing_slash_file", Fd, ENOTDIR);
    if (Fd >= 0)
        close(Fd);
    Fd = open(P("err/file").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    ExpectErrno("errno.excl_existing", Fd, EEXIST);
    if (Fd >= 0)
        close(Fd);
    Fd = open(P("err/file").c_str(), O_RDONLY | O_DIRECTORY);
    ExpectErrno("errno.o_directory_file", Fd, ENOTDIR);
    if (Fd >= 0)
        close(Fd);
    Fd = open(P("err/missing/x").c_str(), O_CREAT | O_WRONLY, 0644);
    ExpectErrno("errno.create_missing_parent", Fd, ENOENT);
    if (Fd >= 0)
        close(Fd);
    Fd = open(P("err/file/x").c_str(), O_CREAT | O_WRONLY, 0644);
    ExpectErrno("errno.create_under_file", Fd, ENOTDIR);
    if (Fd >= 0)
        close(Fd);
    ExpectErrno("errno.stat_missing", access(P("err/missing").c_str(), F_OK), ENOENT);
    Fd = open(P("err/full").c_str(), O_WRONLY);
    ExpectErrno("errno.open_dir_write", Fd, EISDIR);
    if (Fd >= 0)
        close(Fd);
    ExpectErrno("errno.truncate_dir", truncate(P("err/full").c_str(), 0), EISDIR);
    ExpectErrno("errno.rmdir_dot", rmdir(P("err/full/.").c_str()), EINVAL);
}

static void
TestBigDirectory()
{
    const int Count = 3000;
    std::set<std::string> Expected;
    bool Ok = true;

    MakeDir("big");
    for (int Index = 0; Ok && Index < Count; Index++)
    {
        char Name[64];
        snprintf(Name, sizeof(Name), "%s-%05d-%x.dat", Index % 3 ? "Entry" : "zz", Index,
                 Index * 7919);
        int Fd = open(P(std::string("big/") + Name).c_str(), O_CREAT | O_WRONLY, 0644);
        Ok = Fd >= 0;
        if (Fd >= 0)
            close(Fd);
        Expected.insert(Name);
    }
    CHECK("bigdir.create", Ok, "%s", strerror(errno));
    {
        bool TypesOk = mkfifo(P("big/zzz-fifo").c_str(), 0600) == 0 &&
                       symlink("zzz-fifo", P("big/zzz-link").c_str()) == 0;
        DIR* Handle = opendir(P("big").c_str());
        unsigned Seen = 0;
        while (Handle)
        {
            struct dirent* Entry = readdir(Handle);
            if (!Entry)
                break;
            if (!strcmp(Entry->d_name, ".") || !strcmp(Entry->d_name, ".."))
                continue;
            unsigned Type = !strcmp(Entry->d_name, "zzz-fifo") ? DT_FIFO :
                            !strcmp(Entry->d_name, "zzz-link") ? DT_LNK : DT_REG;
            TypesOk = TypesOk && Entry->d_type == Type;
            Seen++;
        }
        if (Handle)
            closedir(Handle);
        CHECK("bigdir.readdir_types", TypesOk && Seen == Count + 2, "checked %u file types", Seen);
        unlink(P("big/zzz-fifo").c_str());
        unlink(P("big/zzz-link").c_str());
    }
    CHECK("bigdir.readdir", ListNames(P("big")) == Expected, "listing differs (%zu vs %zu)",
          ListNames(P("big")).size(), Expected.size());
    {
        int Removed = 0;
        for (auto It = Expected.begin(); It != Expected.end();)
        {
            if ((Removed++ % 2) == 0)
            {
                if (unlink(P("big/" + *It).c_str()) != 0)
                    Ok = false;
                It = Expected.erase(It);
            }
            else
            {
                ++It;
            }
        }
    }
    CHECK("bigdir.unlink_half", Ok && ListNames(P("big")) == Expected, "listing differs");
    {
        DIR* Directory = opendir(P("big").c_str());
        struct dirent* Entry;
        size_t Seen = 0;
        bool DeleteOk = true;
        while ((Entry = readdir(Directory)) != NULL)
        {
            if (!strcmp(Entry->d_name, ".") || !strcmp(Entry->d_name, ".."))
                continue;
            Seen++;
            if (unlink(P(std::string("big/") + Entry->d_name).c_str()) != 0)
                DeleteOk = false;
        }
        closedir(Directory);
        CHECK("bigdir.delete_while_reading",
              DeleteOk && ListNames(P("big")).empty() && Seen == Expected.size(),
              "seen %zu of %zu, %zu left", Seen, Expected.size(), ListNames(P("big")).size());
    }
    CHECK("bigdir.rmdir", rmdir(P("big").c_str()) == 0, "%s", strerror(errno));
}

/* Two walks of one directory that changes between their starts: each keeps the names it started with. */
static void
TestInterleavedScans()
{
    const int Count = 2500;
    std::set<std::string> Stable;
    std::multiset<std::string> SeenFirst;
    std::set<std::string> SeenSecond;
    bool Ok = true;

    MakeDir("scans");
    for (int Index = 0; Ok && Index < Count; Index++)
    {
        char Name[64];

        snprintf(Name, sizeof(Name), "entry-%05d", Index);
        int Fd = open(P(std::string("scans/") + Name).c_str(), O_CREAT | O_WRONLY, 0644);
        Ok = Fd >= 0;
        if (Fd >= 0)
            close(Fd);
        if (Index % 5)
            Stable.insert(Name);
    }
    DIR* First = opendir(P("scans").c_str());
    for (int Index = 0; First && Index < 300; Index++)
    {
        struct dirent* Entry = readdir(First);

        if (Entry && Entry->d_name[0] != '.')
            SeenFirst.insert(Entry->d_name);
    }
    for (int Index = 0; Ok && Index < Count; Index += 5)
    {
        char Name[64];

        snprintf(Name, sizeof(Name), "scans/entry-%05d", Index);
        Ok = unlink(P(Name).c_str()) == 0;
        snprintf(Name, sizeof(Name), "scans/added-%05d", Index);
        int Fd = open(P(Name).c_str(), O_CREAT | O_WRONLY, 0644);
        Ok = Ok && Fd >= 0;
        if (Fd >= 0)
            close(Fd);
    }
    DIR* Second = opendir(P("scans").c_str());
    while (Second)
    {
        struct dirent* Entry = readdir(Second);

        if (!Entry)
            break;
        if (Entry->d_name[0] != '.')
            SeenSecond.insert(Entry->d_name);
    }
    while (First)
    {
        struct dirent* Entry = readdir(First);

        if (!Entry)
            break;
        if (Entry->d_name[0] != '.')
            SeenFirst.insert(Entry->d_name);
    }
    if (First)
        closedir(First);
    if (Second)
        closedir(Second);
    size_t Missing = 0;
    size_t Repeated = 0;
    for (const std::string& Name : Stable)
    {
        Missing += SeenFirst.count(Name) == 0;
        Repeated += SeenFirst.count(Name) > 1;
        Ok = Ok && SeenSecond.count(Name) == 1;
    }
    /* Any offset below one a walk handed out still leads to an entry, never to a false end. */
    {
        DIR* Walk = opendir(P("scans").c_str());
        long Highest = 0;
        bool Found = Walk != NULL;

        for (int Index = 0; Walk && Index < 400; Index++)
        {
            long Position = telldir(Walk);

            if (Position > Highest)
                Highest = Position;
            if (!readdir(Walk))
                break;
        }
        for (int Index = 1; Found && Highest > 0 && Index <= 64; Index++)
        {
            seekdir(Walk, (long)((unsigned long long)Highest * Index / 65));
            Found = readdir(Walk) != NULL;
        }
        if (Walk)
            closedir(Walk);
        CHECK("readdir.offset_below_valid", Found && Highest > 0, "highest offset %ld", Highest);
    }
    CHECK("readdir.interleaved_scans", Ok && First && Second && Missing == 0 && Repeated == 0 &&
          SeenSecond.size() == Stable.size() + Count / 5,
          "first walk missed %zu and repeated %zu, second saw %zu", Missing, Repeated, SeenSecond.size());
    for (const std::string& Name : SeenSecond)
        unlink(P("scans/" + Name).c_str());
    rmdir(P("scans").c_str());
}

static void
TestDeepTree()
{
    std::string Path = "deep";
    std::vector<uint8_t> A = Pattern(51, 0, 333);
    std::vector<uint8_t> Actual;
    bool Ok = true;

    for (int Level = 0; Ok && Level < 60; Level++)
    {
        Ok = mkdir(P(Path).c_str(), 0755) == 0;
        Path += "/d" + std::to_string(Level);
    }
    Ok = Ok && mkdir(P(Path).c_str(), 0755) == 0 && WriteFile(P(Path + "/leaf"), A) &&
         ReadFile(P(Path + "/leaf"), Actual) && Actual == A;
    CHECK("deep.create", Ok, "%s", strerror(errno));
    Ok = unlink(P(Path + "/leaf").c_str()) == 0;
    while (Ok && Path != "deep")
    {
        Ok = rmdir(P(Path).c_str()) == 0;
        Path = Path.substr(0, Path.rfind('/'));
    }
    CHECK("deep.remove", Ok && rmdir(P("deep").c_str()) == 0, "%s", strerror(errno));
}

static void
TestSymlinks()
{
    char Buffer[256];
    ssize_t Length;
    std::vector<uint8_t> Actual;

    MakeDir("sym");
    WriteFile(P("sym/target"), std::vector<uint8_t>{'t'});
    if (symlink("target", P("sym/link").c_str()) != 0)
    {
        FAIL("symlink.create", "%s", strerror(errno));
        return;
    }
    PASS("symlink.create");
    Length = readlink(P("sym/link").c_str(), Buffer, sizeof(Buffer));
    CHECK("symlink.readlink", Length == 6 && !memcmp(Buffer, "target", 6), "length %zd", Length);
    CHECK("symlink.follow", ReadFile(P("sym/link"), Actual) && Actual.size() == 1 &&
                                Actual[0] == 't',
          "following the link failed");
    CHECK("symlink.dangling", symlink("nowhere/x", P("sym/dangling").c_str()) == 0 &&
                                  readlink(P("sym/dangling").c_str(), Buffer, sizeof(Buffer)) == 9,
          "%s", strerror(errno));
    {
        struct stat Stat;
        CHECK("symlink.lstat", lstat(P("sym/link").c_str(), &Stat) == 0 && S_ISLNK(Stat.st_mode),
              "not reported as a link");
    }
    MakeDir("sym/subdir");
    {
        DIR* Directory = opendir(P("sym").c_str());
        struct dirent* Entry;
        bool Ok = Directory != NULL;
        char Detail[600] = "";

        while (Ok && (Entry = readdir(Directory)) != NULL)
        {
            struct stat Stat;
            unsigned char Expected;

            if (!strcmp(Entry->d_name, ".") || !strcmp(Entry->d_name, ".."))
                continue;
            if (lstat(P(std::string("sym/") + Entry->d_name).c_str(), &Stat) != 0)
            {
                snprintf(Detail, sizeof(Detail), "lstat %s: %s", Entry->d_name, strerror(errno));
                Ok = false;
                break;
            }
            Expected = S_ISDIR(Stat.st_mode) ? DT_DIR : S_ISLNK(Stat.st_mode) ? DT_LNK : DT_REG;
            if ((Entry->d_type != DT_UNKNOWN && Entry->d_type != Expected) ||
                Entry->d_ino != Stat.st_ino)
            {
                snprintf(Detail, sizeof(Detail), "%s: d_type %u (want %u) d_ino %llu st_ino %llu",
                         Entry->d_name, Entry->d_type, Expected,
                         (unsigned long long)Entry->d_ino, (unsigned long long)Stat.st_ino);
                Ok = false;
            }
        }
        if (Directory)
            closedir(Directory);
        CHECK("readdir.type_and_ino_match_lstat", Ok, "%s", Detail);
    }
    CHECK("symlink.unlink", unlink(P("sym/link").c_str()) == 0 && Exists(P("sym/target")),
          "unlinking the link touched the target");
}

/* Interleaved symlink churn deletes from internal nodes of the $Reparse view index. */
static void
TestSymlinkChurn()
{
    std::vector<int> Live;
    uint32_t State = 0x2545F491;
    int Next = 0;
    int Failures = 0;
    auto Random = [&State]() {
        State ^= State << 13;
        State ^= State >> 17;
        State ^= State << 5;
        return State;
    };

    MakeDir("churn");
    for (int Step = 0; Step < 6000 && Failures == 0; Step++)
    {
        if (!Live.empty() && Random() % 100 < 45)
        {
            size_t Index = Random() % Live.size();
            std::string Path = P("churn/l" + std::to_string(Live[Index]));

            if (unlink(Path.c_str()) != 0)
                Failures++;
            Live.erase(Live.begin() + (ptrdiff_t)Index);
        }
        else
        {
            std::string Target((Random() % 40) * 10 + 1, 'x');
            std::string Path = P("churn/l" + std::to_string(Next));

            if (symlink(Target.c_str(), Path.c_str()) != 0)
                Failures++;
            Live.push_back(Next++);
        }
    }
    for (int Name : Live)
    {
        if (unlink(P("churn/l" + std::to_string(Name)).c_str()) != 0)
            Failures++;
    }
    CHECK("symlink.churn", Failures == 0 && ListNames(P("churn")).empty(),
          "%d failed operations, %zu names left", Failures, ListNames(P("churn")).size());
}

/* An unprivileged write clears set-user-ID, and set-group-ID when group-executable. */
static void
TestWriteDropsSetId()
{
    struct
    {
        mode_t Mode;
        mode_t Expected;
    } Cases[] = {{04755, 0755}, {02775, 0775}, {06775, 0775}, {02745, 02745}};
    int Failures = 0;
    mode_t Seen = 0;

    if (geteuid() == 0)
    {
        INFO("write.drops_setid", "skipped: running as root");
        return;
    }
    MakeDir("setid");
    for (size_t Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); Index++)
    {
        std::string Path = P("setid/f" + std::to_string(Index));
        struct stat Stat;
        int Fd;

        WriteFile(Path, Pattern(5, 0, 100));
        if (chmod(Path.c_str(), Cases[Index].Mode) != 0)
        {
            Failures++;
            continue;
        }
        Fd = open(Path.c_str(), O_WRONLY);
        if (Fd < 0 || pwrite(Fd, "x", 1, 0) != 1 || fstat(Fd, &Stat) != 0 ||
            (Stat.st_mode & 07777) != Cases[Index].Expected ||
            stat(Path.c_str(), &Stat) != 0 || (Stat.st_mode & 07777) != Cases[Index].Expected)
        {
            Failures++;
            Seen = Stat.st_mode & 07777;
        }
        if (Fd >= 0)
            close(Fd);
    }
    CHECK("write.drops_setid", Failures == 0, "%d cases wrong, last mode %04o", Failures, (unsigned)Seen);

    std::string Path = P("setid/open");
    struct stat Stat = {};
    int Fd;
    bool Written, Truncated;

    WriteFile(Path, Pattern(5, 0, 100));
    Fd = open(Path.c_str(), O_WRONLY);
    Written = Fd >= 0 && pwrite(Fd, "x", 1, 0) == 1 && fchmod(Fd, 04755) == 0 && pwrite(Fd, "y", 1, 1) == 1 &&
              fstat(Fd, &Stat) == 0 && (Stat.st_mode & 07777) == 0755;
    CHECK("write.drops_setid_set_while_open", Written, "mode %04o", (unsigned)(Stat.st_mode & 07777));
    Truncated = Fd >= 0 && fchmod(Fd, 04755) == 0 && ftruncate(Fd, 50) == 0 && fstat(Fd, &Stat) == 0 &&
                (Stat.st_mode & 07777) == 0755;
    CHECK("truncate.drops_setid", Truncated, "mode %04o", (unsigned)(Stat.st_mode & 07777));
    if (Fd >= 0)
        close(Fd);
}

/* A removed directory still open answers fstat with no links and lists nothing. */
static void
TestRemovedOpenDirectory()
{
    struct stat Stat;
    int Fd;
    bool Ok;

    MakeDir("deaddir");
    MakeDir("deaddir/victim");
    MakeDir("deaddir/source");
    Fd = open(P("deaddir/victim").c_str(), O_RDONLY | O_DIRECTORY);
    Ok = Fd >= 0 && rename(P("deaddir/source").c_str(), P("deaddir/victim").c_str()) == 0 &&
         fstat(Fd, &Stat) == 0 && Stat.st_nlink == 0 && S_ISDIR(Stat.st_mode);
    CHECK("dir.renamed_over_open_fstat", Ok, "%s nlink %lu", strerror(errno),
          (unsigned long)Stat.st_nlink);
    if (Fd >= 0)
        close(Fd);

    MakeDir("deaddir/gone");
    Fd = open(P("deaddir/gone").c_str(), O_RDONLY | O_DIRECTORY);
    Ok = Fd >= 0 && rmdir(P("deaddir/gone").c_str()) == 0 && fstat(Fd, &Stat) == 0 &&
         Stat.st_nlink == 0;
    if (Ok)
    {
        DIR* Handle = fdopendir(dup(Fd));
        int Entries = 0;

        Ok = Handle != NULL;
        while (Handle && readdir(Handle))
            Entries++;
        if (Handle)
            closedir(Handle);
        Ok = Ok && Entries == 0;
    }
    CHECK("dir.removed_open_fstat_readdir", Ok, "%s", strerror(errno));
    if (Fd >= 0)
        close(Fd);
}

static void
TestMovedDirectoryListing()
{
    MakeDir("movedir");
    MakeDir("movedir/old");
    MakeDir("movedir/new");
    MakeDir("movedir/old/child");
    DIR* Handle = opendir(P("movedir/old/child").c_str());
    if (!Handle)
    {
        FAIL("readdir.moved_parent", "%s", strerror(errno));
        return;
    }
    while (readdir(Handle))
        ;
    struct stat Parent;
    ino_t ListedParent = 0;
    bool Ok = rename(P("movedir/old/child").c_str(), P("movedir/new/child").c_str()) == 0 &&
              stat(P("movedir/new").c_str(), &Parent) == 0;
    rewinddir(Handle);
    while (struct dirent* Entry = readdir(Handle))
    {
        if (!strcmp(Entry->d_name, ".."))
            ListedParent = Entry->d_ino;
    }
    closedir(Handle);
    CHECK("readdir.moved_parent", Ok && ListedParent == Parent.st_ino, "listed parent %llu",
          (unsigned long long)ListedParent);
}

/* A PATH_MAX - 1 byte target fits one reparse name but not two. */
static void
TestLongSymlinkTarget()
{
    std::string Target(4095, 'a');
    std::vector<char> Read(8192);
    ssize_t Length;

    for (size_t Index = 100; Index < Target.size(); Index += 100)
        Target[Index] = '/';
    MakeDir("longlink");
    Length = symlink(Target.c_str(), P("longlink/l").c_str()) == 0
        ? readlink(P("longlink/l").c_str(), Read.data(), Read.size())
        : -1;
    CHECK("symlink.path_max_target", Length == (ssize_t)Target.size() &&
                                         std::string(Read.data(), (size_t)Length) == Target,
          "%s length %zd", strerror(errno), Length);
}

/* A set-group-ID directory passes the bit to new subdirectories. */
static void
TestSetgidDirectory()
{
    struct stat Parent;
    struct stat Child;
    bool Ok;

    MakeDir("sgid");
    Ok = chmod(P("sgid").c_str(), 02775) == 0 && stat(P("sgid").c_str(), &Parent) == 0 &&
         (Parent.st_mode & S_ISGID) && mkdir(P("sgid/sub").c_str(), 0755) == 0 &&
         stat(P("sgid/sub").c_str(), &Child) == 0;
    CHECK("dir.setgid_inherited", Ok && (Child.st_mode & S_ISGID) && Child.st_gid == Parent.st_gid,
          "%s mode %04o gid %u/%u", strerror(errno), (unsigned)(Child.st_mode & 07777),
          (unsigned)Child.st_gid, (unsigned)Parent.st_gid);
}

/* A long directory name and EAs leave no room for the first index allocation,
 * and a long file name, a user xattr and a second link none for nonresident data. */
static void
TestFullBaseRecords()
{
    std::string Directory = P("fullbase/" + std::string(255, 'd'));
    std::string File = P("fullbase/" + std::string(221, 'f'));
    std::vector<uint8_t> Data = Pattern(37, 0, 17938);
    std::vector<uint8_t> Actual;
    std::string Value(64, 'v');
    bool Ok;
    int Fd;

    MakeDir("fullbase");
    MakeDir("fullbase/" + std::string(255, 'd'));
    Ok = setxattr(Directory.c_str(), "user.big", std::string(600, 'b').data(), 600, 0) == 0 ||
         errno == ENOTSUP;
    for (int Index = 0; Ok && Index < 41; Index++)
        Ok = WriteFile(Directory + "/entry-" + std::string(60, 'e') + "-" + std::to_string(Index), {});
    CHECK("dir.first_index_allocation_full_base", Ok && ListNames(Directory).size() == 41,
          "%s, %zu names", strerror(errno), ListNames(Directory).size());

    Ok = WriteFile(File, {}) &&
         setxattr(File.c_str(), "user.x1", Value.data(), Value.size(), 0) == 0 &&
         link(File.c_str(), P("fullbase/link").c_str()) == 0;
    Fd = Ok ? open(P("fullbase/link").c_str(), O_WRONLY) : -1;
    Ok = Fd >= 0 && pwrite(Fd, Data.data(), Data.size(), 903279) == (ssize_t)Data.size();
    if (Fd >= 0)
        close(Fd);
    Ok = Ok && ReadFile(File, Actual) && Actual.size() == 903279 + Data.size() &&
         std::equal(Data.begin(), Data.end(), Actual.begin() + 903279);
    CHECK("write.promote_full_base", Ok, "%s", strerror(errno));
}

/* Fragmented growth beside a long name moves $DATA into an extension record. */
static void
TestRenameExtensionData()
{
    std::vector<uint8_t> Block = Pattern(31, 0, 4096);
    std::string From = P("extdata/" + std::string(255, 'q'));
    std::string Filler = P("extdata/filler");
    bool Ok = true;

    MakeDir("extdata");
    for (int Index = 0; Ok && Index < 64; Index++)
    {
        for (const std::string& Path : {From, Filler})
        {
            int Fd = open(Path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

            Ok = Fd >= 0 && write(Fd, Block.data(), Block.size()) == (ssize_t)Block.size();
            if (Fd >= 0)
                close(Fd);
        }
    }
    Ok = Ok && rename(From.c_str(), P("extdata/" + std::string(255, 'r')).c_str()) == 0;
    CHECK("rename.extension_data_setup", Ok, "%s", strerror(errno));
}

/* posix_edge_smoke.sh checks the index sizes of these resident files. */
static void
TestResidentIndexSizes()
{
    const size_t Sizes[] = {1, 5, 13, 100, 450};
    bool Ok = true;

    MakeDir("dupsize");
    MakeDir("dupsize/sub");
    for (size_t Index = 0; Index < sizeof(Sizes) / sizeof(Sizes[0]); Index++)
    {
        std::string Path = P("dupsize/f" + std::to_string(Index));

        WriteFile(Path, Pattern(9, 0, Sizes[Index]));
        Ok = Ok && truncate(Path.c_str(), (off_t)Sizes[Index] + 3) == 0;
    }
    Ok = Ok && rename(P("dupsize/f1").c_str(), P("dupsize/renamed").c_str()) == 0 &&
         rename(P("dupsize/f2").c_str(), P("dupsize/sub/moved").c_str()) == 0 &&
         link(P("dupsize/f3").c_str(), P("dupsize/sub/linked").c_str()) == 0;
    CHECK("index.resident_sizes_setup", Ok, "%s", strerror(errno));
}

/* posix_edge_smoke.sh compares the index entries these leave after the last close. */
static void
TestCloseTimeNames()
{
    std::vector<uint8_t> Block = Pattern(43, 0, 5000);
    std::vector<uint8_t> Whole = Pattern(43, 0, 15000);
    const struct timespec Times[2] = {{1000000000, 0}, {1000000000, 0}};
    bool Ok;
    int Fd;

    MakeDir("closenames");
    Fd = open(P("closenames/a").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    Ok = Fd >= 0;
    for (int Index = 0; Ok && Index < 3; Index++)
        Ok = write(Fd, Block.data(), Block.size()) == (ssize_t)Block.size();
    Ok = Ok && fchmod(Fd, 0640) == 0 && fsetxattr(Fd, "user.closenames", "v", 1, 0) == 0 &&
         futimens(Fd, Times) == 0;
    if (Fd >= 0)
        close(Fd);
    Fd = Ok ? open(P("closenames/b").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640) : -1;
    Ok = Fd >= 0 && write(Fd, Whole.data(), Whole.size()) == (ssize_t)Whole.size() &&
         futimens(Fd, Times) == 0;
    if (Fd >= 0)
        close(Fd);
    Ok = Ok && setxattr(P("closenames/b").c_str(), "user.closenames", "v", 1, 0) == 0;
    CHECK("names.close_time_setup", Ok, "%s", strerror(errno));
}

/* POSIX: after rewinddir, names added since opendir must be returned. */
static void
TestRewinddirSeesNewNames()
{
    std::set<std::string> Seen;
    struct dirent* Entry;
    DIR* Handle;

    MakeDir("rewind");
    Handle = opendir(P("rewind").c_str());
    if (!Handle)
    {
        CHECK("readdir.rewind_new_names", false, "%s", strerror(errno));
        return;
    }
    while (readdir(Handle))
        ;
    for (int Index = 0; Index < 300; Index++)
        WriteFile(P("rewind/n" + std::to_string(Index)), {});
    rewinddir(Handle);
    while ((Entry = readdir(Handle)) != NULL)
    {
        if (strcmp(Entry->d_name, ".") != 0 && strcmp(Entry->d_name, "..") != 0)
            Seen.insert(Entry->d_name);
    }
    closedir(Handle);
    CHECK("readdir.rewind_new_names", Seen.size() == 300, "%zu of 300 names after rewinddir", Seen.size());
}

/* A rename holds both long $FILE_NAMEs in the record next to the POSIX EAs. */
static void
TestRenameLongNames()
{
    const size_t Lengths[][2] = {{200, 255}, {255, 255}, {254, 200}, {8, 255}};
    std::vector<uint8_t> Data = Pattern(23, 0, 777);
    std::vector<uint8_t> Actual;
    int Failures = 0;

    MakeDir("longren");
    for (size_t Index = 0; Index < sizeof(Lengths) / sizeof(Lengths[0]); Index++)
    {
        std::string From = P("longren/" + std::string(Lengths[Index][0], (char)('a' + Index)));
        std::string To = P("longren/" + std::string(Lengths[Index][1], (char)('p' + Index)));

        WriteFile(From, Data);
        if (rename(From.c_str(), To.c_str()) != 0 || !ReadFile(To, Actual) || Actual != Data ||
            access(From.c_str(), F_OK) == 0)
        {
            Failures++;
        }
    }
    CHECK("rename.long_names", Failures == 0 && ListNames(P("longren")).size() == 4,
          "%d failed renames (%s)", Failures, strerror(errno));
}

/* Many long link names push $FILE_NAME attributes into extension records. */
static void
TestRenameManyLinks()
{
    std::vector<uint8_t> Data = Pattern(17, 0, 3000);
    std::vector<uint8_t> Actual;
    std::string Long(150, 'n');
    bool Ok = true;

    MakeDir("manylinks");
    WriteFile(P("manylinks/base"), Data);
    for (int Index = 0; Ok && Index < 40; Index++)
        Ok = link(P("manylinks/base").c_str(),
                  P("manylinks/" + Long + std::to_string(Index)).c_str()) == 0;
    for (int Index = 0; Ok && Index < 40; Index++)
        Ok = rename(P("manylinks/" + Long + std::to_string(Index)).c_str(),
                    P("manylinks/r" + Long + std::to_string(Index)).c_str()) == 0;
    struct stat Stat;
    Ok = Ok && stat(P("manylinks/base").c_str(), &Stat) == 0 && Stat.st_nlink == 41 &&
         ReadFile(P("manylinks/r" + Long + "39"), Actual) && Actual == Data &&
         ListNames(P("manylinks")).size() == 41;
    CHECK("link.rename_many_long_links", Ok, "%s nlink %lu", strerror(errno),
          (unsigned long)Stat.st_nlink);

    std::vector<uint8_t> More = Pattern(18, 0, 5000);
    int Fd = open(P("manylinks/base").c_str(), O_WRONLY | O_APPEND);
    bool Appended = Fd >= 0 && write(Fd, More.data(), More.size()) == (ssize_t)More.size();

    if (Fd >= 0)
        close(Fd);
    CHECK("link.size_change_with_many_links", Appended, "%s", strerror(errno));
}

static void
TestModes()
{
    struct stat Stat;

    MakeDir("mode");
    WriteFile(P("mode/f"), std::vector<uint8_t>{1});
    stat(P("mode/f").c_str(), &Stat);
    CHECK("mode.new_file_writable", (Stat.st_mode & 0200) != 0, "mode %o", Stat.st_mode & 07777);
    stat(P("mode").c_str(), &Stat);
    CHECK("mode.new_dir_writable", (Stat.st_mode & 0200) != 0, "mode %o", Stat.st_mode & 07777);
    CHECK("mode.chmod_readonly", chmod(P("mode/f").c_str(), 0444) == 0 &&
                                     stat(P("mode/f").c_str(), &Stat) == 0 &&
                                     (Stat.st_mode & 0222) == 0,
          "mode %o after chmod 0444", Stat.st_mode & 07777);
    if (geteuid() != 0)
    {
        int Fd = open(P("mode/f").c_str(), O_WRONLY);
        ExpectErrno("mode.readonly_refuses_write_open", Fd, EACCES);
        if (Fd >= 0)
            close(Fd);
        ExpectErrno("mode.readonly_access_w", access(P("mode/f").c_str(), W_OK), EACCES);
    }
    WriteFile(P("mode/g"), std::vector<uint8_t>{2});
    chmod(P("mode/g").c_str(), 0444);
    CHECK("mode.unlink_readonly", unlink(P("mode/g").c_str()) == 0, "%s", strerror(errno));
    {
        mode_t Old = umask(022);
        int Fd = open(P("mode/m640").c_str(), O_CREAT | O_WRONLY, 0640);

        if (Fd >= 0)
            close(Fd);
        CHECK("mode.create_mode_kept", stat(P("mode/m640").c_str(), &Stat) == 0 &&
                                           (Stat.st_mode & 07777) == 0640,
              "mode %o", Stat.st_mode & 07777);
        CHECK("mode.mkdir_mode_kept", mkdir(P("mode/d750").c_str(), 0750) == 0 &&
                                          stat(P("mode/d750").c_str(), &Stat) == 0 &&
                                          (Stat.st_mode & 07777) == 0750,
              "mode %o", Stat.st_mode & 07777);
        CHECK("mode.mkfifo", mkfifo(P("mode/fifo").c_str(), 0640) == 0 &&
                                 lstat(P("mode/fifo").c_str(), &Stat) == 0 &&
                                 S_ISFIFO(Stat.st_mode) && (Stat.st_mode & 07777) == 0640,
              "%s mode %o", strerror(errno), Stat.st_mode);
        CHECK("mode.chmod_kept", chmod(P("mode/m640").c_str(), 0604) == 0 &&
                                     stat(P("mode/m640").c_str(), &Stat) == 0 &&
                                     (Stat.st_mode & 07777) == 0604,
              "mode %o", Stat.st_mode & 07777);
        umask(Old);
    }
    CHECK("mode.chmod_writable", chmod(P("mode/f").c_str(), 0644) == 0 &&
                                     stat(P("mode/f").c_str(), &Stat) == 0 &&
                                     (Stat.st_mode & 0200) != 0,
          "mode %o after chmod 0644", Stat.st_mode & 07777);
}

static void
TestTimes()
{
    struct timespec Times[2];
    struct stat Stat;
    std::string Path = P("times");

    WriteFile(Path, std::vector<uint8_t>{1, 2, 3});
    CHECK("time.new_file_write_after_creation", stat(Path.c_str(), &Stat) == 0 &&
          (Stat.st_mtim.tv_sec > Stat.st_atim.tv_sec ||
           (Stat.st_mtim.tv_sec == Stat.st_atim.tv_sec && Stat.st_mtim.tv_nsec >= Stat.st_atim.tv_nsec)),
          "atime %lld.%09ld mtime %lld.%09ld", (long long)Stat.st_atim.tv_sec, Stat.st_atim.tv_nsec,
          (long long)Stat.st_mtim.tv_sec, Stat.st_mtim.tv_nsec);
    Times[0].tv_sec = 981173106;
    Times[0].tv_nsec = 123456789;
    Times[1].tv_sec = 1000000000;
    Times[1].tv_nsec = 987654321;
    CHECK("time.utimensat", utimensat(AT_FDCWD, Path.c_str(), Times, 0) == 0, "%s",
          strerror(errno));
    stat(Path.c_str(), &Stat);
    CHECK("time.mtime_100ns", Stat.st_mtim.tv_sec == 1000000000 &&
                                  Stat.st_mtim.tv_nsec == 987654300,
          "mtime %lld.%09ld", (long long)Stat.st_mtim.tv_sec, Stat.st_mtim.tv_nsec);
    CHECK("time.atime_100ns", Stat.st_atim.tv_sec == 981173106 &&
                                  Stat.st_atim.tv_nsec == 123456700,
          "atime %lld.%09ld", (long long)Stat.st_atim.tv_sec, Stat.st_atim.tv_nsec);
    if (sizeof(time_t) >= 8)
    {
        struct timespec Limit[2] = {{0, UTIME_OMIT}, {(time_t)INT64_C(910692730085), 477580800}};
        struct stat After = {};
        int Result = utimensat(AT_FDCWD, Path.c_str(), Limit, 0);
        int Error = errno;
        CHECK("time.ntfs_upper_bound", stat(Path.c_str(), &After) == 0 &&
              ((Result == 0 && After.st_mtim.tv_sec == Limit[1].tv_sec &&
                After.st_mtim.tv_nsec == 477580700) ||
               (Result < 0 && Error == EOVERFLOW && After.st_mtim.tv_sec == Stat.st_mtim.tv_sec &&
                After.st_mtim.tv_nsec == Stat.st_mtim.tv_nsec)),
              "result %d errno %d mtime %lld.%09ld", Result, Error,
              (long long)After.st_mtim.tv_sec, After.st_mtim.tv_nsec);
    }
    Times[0].tv_nsec = UTIME_OMIT;
    Times[1].tv_sec = 1100000000;
    Times[1].tv_nsec = 0;
    utimensat(AT_FDCWD, Path.c_str(), Times, 0);
    stat(Path.c_str(), &Stat);
    CHECK("time.utime_omit", Stat.st_atim.tv_sec == 981173106 && Stat.st_mtim.tv_sec == 1100000000,
          "atime %lld mtime %lld", (long long)Stat.st_atim.tv_sec, (long long)Stat.st_mtim.tv_sec);
    Times[0].tv_sec = -86400;
    Times[0].tv_nsec = 0;
    Times[1].tv_sec = -86400 * 365;
    Times[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, Path.c_str(), Times, 0) == 0)
    {
        stat(Path.c_str(), &Stat);
        CHECK("time.pre_1970", Stat.st_mtim.tv_sec == -86400 * 365, "mtime %lld",
              (long long)Stat.st_mtim.tv_sec);
    }
    else
    {
        INFO("time.pre_1970", "%s", strerror(errno));
    }

    {
        Times[0].tv_sec = Times[1].tv_sec = 946684800;
        Times[0].tv_nsec = Times[1].tv_nsec = 0;
        utimensat(AT_FDCWD, Path.c_str(), Times, 0);
        int Fd = open(Path.c_str(), O_WRONLY);
        pwrite(Fd, "z", 1, 1);
        close(Fd);
        stat(Path.c_str(), &Stat);
        CHECK("time.write_updates_mtime", Stat.st_mtim.tv_sec > 946684800, "mtime %lld",
              (long long)Stat.st_mtim.tv_sec);
        utimensat(AT_FDCWD, Path.c_str(), Times, 0);
        truncate(Path.c_str(), 1);
        stat(Path.c_str(), &Stat);
        CHECK("time.truncate_updates_mtime", Stat.st_mtim.tv_sec > 946684800, "mtime %lld",
              (long long)Stat.st_mtim.tv_sec);
    }
    {
        struct timespec Old[2] = {{978307200, 0}, {1009843200, 0}};
        char Byte;

        utimensat(AT_FDCWD, Path.c_str(), Old, 0);
        int Fd = open(Path.c_str(), O_RDONLY);
        bool Read = Fd >= 0 && pread(Fd, &Byte, 1, 0) == 1;
        if (Fd >= 0)
            close(Fd);
        stat(Path.c_str(), &Stat);
        CHECK("time.relatime_read_updates_old_atime", Read && Stat.st_atim.tv_sec > 1009843200,
              "atime %lld", (long long)Stat.st_atim.tv_sec);

        struct timespec First = Stat.st_atim;

        Fd = open(Path.c_str(), O_RDONLY);
        Read = Fd >= 0 && pread(Fd, &Byte, 1, 0) == 1;
        if (Fd >= 0)
            close(Fd);
        stat(Path.c_str(), &Stat);
        CHECK("time.relatime_second_read_keeps_atime",
              Read && Stat.st_atim.tv_sec == First.tv_sec && Stat.st_atim.tv_nsec == First.tv_nsec,
              "atime moved from %lld.%09ld to %lld.%09ld", (long long)First.tv_sec, First.tv_nsec,
              (long long)Stat.st_atim.tv_sec, Stat.st_atim.tv_nsec);

        /* xfstests generic/003: a read after a write moves atime even when the written pages are cached. */
        for (int Attempt = 0; Attempt < 1000; ++Attempt)
        {
            struct timespec Now;
            clock_gettime(CLOCK_REALTIME_COARSE, &Now);
            if (Now.tv_sec > First.tv_sec || (Now.tv_sec == First.tv_sec && Now.tv_nsec / 100 > First.tv_nsec / 100))
                break;
            usleep(1000);
        }
        Fd = open(Path.c_str(), O_WRONLY);
        bool Wrote = Fd >= 0 && pwrite(Fd, "w", 1, 0) == 1;
        if (Fd >= 0)
            close(Fd);
        stat(Path.c_str(), &Stat);
        First = Stat.st_atim;
        Fd = open(Path.c_str(), O_RDONLY);
        Read = Fd >= 0 && pread(Fd, &Byte, 1, 0) == 1;
        if (Fd >= 0)
            close(Fd);
        stat(Path.c_str(), &Stat);
        CHECK("time.relatime_read_after_write_moves_atime",
              Wrote && Read && (Stat.st_atim.tv_sec > First.tv_sec ||
                                (Stat.st_atim.tv_sec == First.tv_sec && Stat.st_atim.tv_nsec > First.tv_nsec)),
              "atime stayed %lld.%09ld", (long long)First.tv_sec, First.tv_nsec);
    }
    {
        MakeDir("tdir");
        Times[0].tv_sec = Times[1].tv_sec = 946684800;
        utimensat(AT_FDCWD, P("tdir").c_str(), Times, 0);
        WriteFile(P("tdir/new"), std::vector<uint8_t>{1});
        stat(P("tdir").c_str(), &Stat);
        CHECK("time.dir_mtime_on_create", Stat.st_mtim.tv_sec > 946684800, "dir mtime %lld",
              (long long)Stat.st_mtim.tv_sec);
        utimensat(AT_FDCWD, P("tdir").c_str(), Times, 0);
        unlink(P("tdir/new").c_str());
        stat(P("tdir").c_str(), &Stat);
        CHECK("time.dir_mtime_on_unlink", Stat.st_mtim.tv_sec > 946684800, "dir mtime %lld",
              (long long)Stat.st_mtim.tv_sec);
    }
}

static void
TestXattrs()
{
    std::string Path = P("xattr");
    std::vector<char> Value(60000);
    char Buffer[65536];
    ssize_t Length;

    WriteFile(Path, std::vector<uint8_t>{1});
    if (setxattr(Path.c_str(), "user.alpha", "one", 3, 0) != 0)
    {
        INFO("xattr.user", "unsupported: %s", strerror(errno));
        return;
    }
    Length = getxattr(Path.c_str(), "user.alpha", Buffer, sizeof(Buffer));
    CHECK("xattr.roundtrip", Length == 3 && !memcmp(Buffer, "one", 3), "length %zd", Length);
    CHECK("xattr.empty_value", setxattr(Path.c_str(), "user.empty", "", 0, 0) == 0 &&
                                   getxattr(Path.c_str(), "user.empty", Buffer, sizeof(Buffer)) == 0,
          "%s", strerror(errno));
    for (size_t Index = 0; Index < Value.size(); Index++)
        Value[Index] = (char)PatternByte(61, Index);
    if (setxattr(Path.c_str(), "user.big", Value.data(), Value.size(), 0) == 0)
    {
        Length = getxattr(Path.c_str(), "user.big", Buffer, sizeof(Buffer));
        CHECK("xattr.large", Length == (ssize_t)Value.size() &&
                                 !memcmp(Buffer, Value.data(), Value.size()),
              "length %zd", Length);
    }
    else
    {
        INFO("xattr.large", "60000-byte value refused: %s", strerror(errno));
    }
    ExpectErrno("xattr.create_existing",
                setxattr(Path.c_str(), "user.alpha", "x", 1, XATTR_CREATE), EEXIST);
    ExpectErrno("xattr.replace_missing",
                setxattr(Path.c_str(), "user.nothere", "x", 1, XATTR_REPLACE), ENODATA);
    Length = getxattr(Path.c_str(), "user.alpha", Buffer, 1);
    ExpectErrno("xattr.small_buffer", (int)Length, ERANGE);
    CHECK("xattr.remove", removexattr(Path.c_str(), "user.alpha") == 0 &&
                              getxattr(Path.c_str(), "user.alpha", Buffer, sizeof(Buffer)) < 0 &&
                              errno == ENODATA,
          "%s", strerror(errno));
    ExpectErrno("xattr.remove_missing", removexattr(Path.c_str(), "user.alpha"), ENODATA);
    Length = listxattr(Path.c_str(), Buffer, sizeof(Buffer));
    {
        std::set<std::string> Names;
        for (ssize_t Offset = 0; Length > 0 && Offset < Length;
             Offset += (ssize_t)strlen(Buffer + Offset) + 1)
            Names.insert(Buffer + Offset);
        INFO("xattr.list", "%zu names%s", Names.size(),
             Names.count("user.empty") ? "" : " (user.empty missing)");
    }
    {
        std::string Dir = P("xattrdir");
        MakeDir("xattrdir");
        CHECK("xattr.directory", setxattr(Dir.c_str(), "user.d", "dv", 2, 0) == 0 &&
                                     getxattr(Dir.c_str(), "user.d", Buffer, sizeof(Buffer)) == 2,
              "%s", strerror(errno));
    }
}

static void
TestMmap()
{
    std::string Path = P("mmap.bin");
    std::vector<uint8_t> Data = Pattern(71, 0, 3 * 4096 + 100);
    std::vector<uint8_t> Actual;
    void* Map;
    int Fd;

    WriteFile(Path, Data);
    Fd = open(Path.c_str(), O_RDONLY);
    Map = mmap(NULL, Data.size(), PROT_READ, MAP_SHARED, Fd, 0);
    if (Map == MAP_FAILED)
    {
        FAIL("mmap.shared_read", "%s", strerror(errno));
    }
    else
    {
        CHECK("mmap.shared_read", !memcmp(Map, Data.data(), Data.size()), "content differs");
        munmap(Map, Data.size());
    }
    close(Fd);

    Fd = open(Path.c_str(), O_RDWR);
    Map = mmap(NULL, Data.size(), PROT_READ | PROT_WRITE, MAP_SHARED, Fd, 0);
    if (Map == MAP_FAILED)
    {
        FAIL("mmap.shared_write", "%s", strerror(errno));
    }
    else
    {
        memcpy((uint8_t*)Map + 5000, "MMAP", 4);
        msync(Map, Data.size(), MS_SYNC);
        munmap(Map, Data.size());
        memcpy(Data.data() + 5000, "MMAP", 4);
        CHECK("mmap.shared_write", ReadFile(Path, Actual) && Actual == Data,
              "msync'd data not visible through read");
    }
    close(Fd);

    Fd = open(Path.c_str(), O_RDONLY);
    Map = mmap(NULL, Data.size(), PROT_READ, MAP_PRIVATE, Fd, 0);
    CHECK("mmap.private_read", Map != MAP_FAILED && !memcmp(Map, Data.data(), Data.size()),
          "%s", Map == MAP_FAILED ? strerror(errno) : "content differs");
    if (Map != MAP_FAILED)
        munmap(Map, Data.size());
    close(Fd);
}

/* xfstests generic/346: write() and a shared mapping of other bytes in the same pages both land. */
static void
TestMmapWriteRace()
{
    const long Pages = 256;
    const long PageSize = sysconf(_SC_PAGESIZE);
    std::string Path = P("mmaprace.bin");
    long Lost = 0;
    bool Ok = true;

    for (uint64_t Round = 0; Ok && Round < 20; Round++)
    {
        const uint64_t Written = UINT64_C(0x5752495445000000) | Round;
        const uint64_t Mapped = UINT64_C(0x4D4D415000000000) | Round;
        int Fd = open(Path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        char* Map = (char*)MAP_FAILED;
        pid_t Child = -1;
        int Status = 0;

        Ok = Fd >= 0 && ftruncate(Fd, Pages * PageSize) == 0;
        if (Ok)
            Map = (char*)mmap(NULL, Pages * PageSize, PROT_READ | PROT_WRITE, MAP_SHARED, Fd, 0);
        Ok = Ok && Map != MAP_FAILED && (Child = fork()) >= 0;
        if (Child == 0)
        {
            for (long Page = 0; Page < Pages; Page++)
                memcpy(Map + Page * PageSize + sizeof(Written), &Mapped, sizeof(Mapped));
            _exit(msync(Map, Pages * PageSize, MS_SYNC) == 0 ? 0 : 1);
        }
        for (long Page = 0; Ok && Page < Pages; Page++)
            Ok = pwrite(Fd, &Written, sizeof(Written), Page * PageSize) == (ssize_t)sizeof(Written);
        Ok = Ok && waitpid(Child, &Status, 0) == Child && WIFEXITED(Status) && WEXITSTATUS(Status) == 0;
        if (Map != MAP_FAILED)
            munmap(Map, Pages * PageSize);
        for (long Page = 0; Ok && Page < Pages; Page++)
        {
            uint64_t Values[2];

            Ok = pread(Fd, Values, sizeof(Values), Page * PageSize) == (ssize_t)sizeof(Values);
            Lost += Ok && (Values[0] != Written || Values[1] != Mapped);
        }
        if (Fd >= 0)
            close(Fd);
    }
    unlink(Path.c_str());
    CHECK("mmap.write_race", Ok && Lost == 0, "%ld pages lost a write (%s)", Lost, strerror(errno));
}

static double
NowSeconds()
{
    struct timespec Now;

    clock_gettime(CLOCK_MONOTONIC, &Now);
    return (double)Now.tv_sec + (double)Now.tv_nsec / 1e9;
}

/* A write far beyond valid data zero-fills the gap below it; opens of an
 * unrelated file must not wait for that fill.
 */
static void
TestGapFill()
{
    const off_t Size = 16 * 1048576;
    std::string Path = P("gap.bin");
    std::vector<uint8_t> Page = Pattern(101, 0, 4096);
    std::atomic<bool> Stop(false);
    std::atomic<double> WorstOpen(0);
    std::atomic<unsigned> Opens(0);
    double Start, Elapsed;
    int Fd;

    WriteFile(P("gap-other"), std::vector<uint8_t>{1, 2, 3});
    Fd = open(Path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK("gap.set_size", Fd >= 0 && ftruncate(Fd, Size) == 0, "%s", strerror(errno));
    std::thread Opener([&]() {
        std::string Other = P("gap-other");
        while (!Stop.load())
        {
            double Before = NowSeconds();
            int OtherFd = open(Other.c_str(), O_RDONLY);
            if (OtherFd >= 0)
                close(OtherFd);
            double Took = NowSeconds() - Before;
            if (Took > WorstOpen.load())
                WorstOpen.store(Took);
            Opens++;
        }
    });
    usleep(20000);
    Start = NowSeconds();
    bool Ok = WriteAll(Fd, Page.data(), Page.size(), Size - 4096) && fsync(Fd) == 0;
    Elapsed = NowSeconds() - Start;
    Stop.store(true);
    Opener.join();
    CHECK("gap.write_last_page", Ok, "%s", strerror(errno));
    INFO("gap.timing", "fill+flush %.3f ms, worst open %.3f ms over %u opens", Elapsed * 1e3,
         WorstOpen.load() * 1e3, Opens.load());
    if (getenv("POSIX_EDGE_SERIALIZED_FS"))
        INFO("gap.opens_do_not_wait", "skipped: the file system serves one request at a time");
    else if (Elapsed > 0.005)
        CHECK("gap.opens_do_not_wait", WorstOpen.load() * 2 < Elapsed,
              "worst open %.3f ms during a %.3f ms fill", WorstOpen.load() * 1e3, Elapsed * 1e3);
    close(Fd);
    {
        std::vector<uint8_t> Actual;
        bool DataOk = ReadFile(Path, Actual) && (off_t)Actual.size() == Size &&
                      std::equal(Page.begin(), Page.end(), Actual.end() - 4096);
        for (off_t Index = 0; DataOk && Index < Size - 4096; Index++)
            DataOk = Actual[(size_t)Index] == 0;
        CHECK("gap.content", DataOk, "size %zu or content wrong", Actual.size());
    }
    unlink(Path.c_str());
    unlink(P("gap-other").c_str());
}

/* xfstests generic/451: buffered readers must not keep pages an asynchronous O_DIRECT write replaced. */
static void
TestAsyncDirectWrite()
{
    const size_t Size = 256 * 1024;
    std::string Path = P("aiodirect.bin");
    aio_context_t Context = 0;
    void* Aligned = NULL;
    long Stale = 0;
    int Direct = -1;
    int Reader = -1;
    bool Ok = WriteFile(Path, std::vector<uint8_t>(Size, 0x55)) && posix_memalign(&Aligned, 4096, Size) == 0 &&
              syscall(SYS_io_setup, 1, &Context) == 0;

    if (Ok)
    {
        Direct = open(Path.c_str(), O_RDWR | O_DIRECT);
        Reader = open(Path.c_str(), O_RDONLY);
        Ok = Direct >= 0 && Reader >= 0;
    }
    for (int Round = 0; Ok && Round < 64; Round++)
    {
        const uint8_t Value = (uint8_t)(0x80 | Round);
        struct iocb Block = {};
        struct iocb* Blocks[1] = {&Block};
        struct io_event Event = {};
        std::atomic<bool> Done(false);

        memset(Aligned, Value, Size);
        Block.aio_fildes = (uint32_t)Direct;
        Block.aio_lio_opcode = IOCB_CMD_PWRITE;
        Block.aio_buf = (uint64_t)(uintptr_t)Aligned;
        Block.aio_nbytes = Size;
        std::thread Readers([&] {
            char Page[4096];

            while (!Done)
            {
                for (size_t Offset = 0; Offset < Size; Offset += sizeof(Page))
                    (void)!pread(Reader, Page, sizeof(Page), (off_t)Offset);
            }
        });
        Ok = syscall(SYS_io_submit, Context, 1, Blocks) == 1 &&
             syscall(SYS_io_getevents, Context, 1, 1, &Event, NULL) == 1 && Event.res == (long long)Size;
        Done = true;
        Readers.join();
        for (size_t Offset = 0; Ok && Offset < Size; Offset += 4096)
        {
            uint8_t Byte;

            Ok = pread(Reader, &Byte, 1, (off_t)Offset) == 1;
            Stale += Ok && Byte != Value;
        }
    }
    if (Context)
        syscall(SYS_io_destroy, Context);
    if (Direct >= 0)
        close(Direct);
    if (Reader >= 0)
        close(Reader);
    free(Aligned);
    unlink(Path.c_str());
    CHECK("aio.direct_write_coherent", Ok && Stale == 0, "%ld stale pages (%s)", Stale, strerror(errno));
}

static void
TestCoherence()
{
    std::string Path = P("coherent.bin");
    char Buffer[8];
    int Writer, Reader;

    WriteFile(Path, Pattern(81, 0, 9000));
    Reader = open(Path.c_str(), O_RDONLY);
    pread(Reader, Buffer, 4, 0);
    Writer = open(Path.c_str(), O_WRONLY);
    pwrite(Writer, "NEW!", 4, 0);
    CHECK("coherence.two_handles", pread(Reader, Buffer, 4, 0) == 4 && !memcmp(Buffer, "NEW!", 4),
          "reader saw stale data");
    ftruncate(Writer, 2);
    CHECK("coherence.truncate_other_handle", pread(Reader, Buffer, 4, 0) == 2,
          "reader saw stale size");
    close(Writer);
    close(Reader);
    {
        int Fd = open(Path.c_str(), O_WRONLY | O_TRUNC);
        close(Fd);
        CHECK("open.o_trunc", SizeOf(Path) == 0, "size %lld", (long long)SizeOf(Path));
        WriteFile(Path, Pattern(82, 0, 7000));
        Fd = open(Path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        bool Ok = Fd >= 0 && write(Fd, "abc", 3) == 3;
        if (Fd >= 0)
            close(Fd);
        CHECK("open.creat_trunc_existing", Ok && SizeOf(Path) == 3, "size %lld",
              (long long)SizeOf(Path));
    }
    {
        std::vector<uint8_t> Victim(100000, 'V');
        std::vector<uint8_t> Other(100000, 'O');
        std::vector<uint8_t> Patch(50000, 'Z');
        std::vector<uint8_t> Actual;
        WriteFile(P("victim"), Victim);
        int Fd = open(P("victim").c_str(), O_RDWR);
        bool Ok = truncate(P("victim").c_str(), 0) == 0 && WriteFile(P("other"), Other) &&
                  WriteAll(Fd, Patch.data(), Patch.size(), 0);
        close(Fd);
        CHECK("coherence.path_truncate_then_handle_write",
              Ok && ReadFile(P("other"), Actual) && Actual == Other &&
                  SizeOf(P("victim")) == 50000,
              "other intact %d, victim size %lld", Actual == Other,
              (long long)SizeOf(P("victim")));
        unlink(P("victim").c_str());
        unlink(P("other").c_str());
    }
}

static void
TestStatfs()
{
    struct statvfs Before, After, Released;
    std::vector<uint8_t> Data = Pattern(91, 0, 8 * 1048576);

    sync();
    statvfs(Root.c_str(), &Before);
    WriteFile(P("space.bin"), Data);
    sync();
    statvfs(Root.c_str(), &After);
    CHECK("statfs.write_consumes",
          (Before.f_bfree - After.f_bfree) * After.f_frsize >= Data.size(),
          "free dropped by %llu bytes",
          (unsigned long long)((Before.f_bfree - After.f_bfree) * After.f_frsize));
    unlink(P("space.bin").c_str());
    sync();
    statvfs(Root.c_str(), &Released);
    CHECK("statfs.unlink_releases",
          Released.f_bfree + 8 >= Before.f_bfree,
          "before %llu after release %llu",
          (unsigned long long)Before.f_bfree, (unsigned long long)Released.f_bfree);
}

static void
TestEnospc()
{
    std::string Path = P("fill.bin");
    std::vector<uint8_t> Chunk(1048576, 0x5A);
    struct statvfs Before, Released;
    off_t Offset = 0;
    off_t Durable = 0;
    int Error = 0;
    int Fd;

    sync();
    statvfs(Root.c_str(), &Before);
    Fd = open(Path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    while (Fd >= 0 && (uint64_t)Offset <= (uint64_t)Before.f_blocks * Before.f_frsize)
    {
        ssize_t Done = pwrite(Fd, Chunk.data(), Chunk.size(), Offset);
        if (Done < 0)
        {
            Error = errno;
            break;
        }
        if (Done == 0)
            break;
        Offset += Done;
        if (fsync(Fd) < 0)
        {
            Error = errno;
            break;
        }
        Durable = Offset;
    }
    if (close(Fd) < 0 && !Error)
        Error = errno;
    CHECK("enospc.errno", Error == ENOSPC, "write or synchronization failed with %s after %lld bytes",
          Error ? strerror(Error) : "nothing", (long long)Offset);
    off_t Size = SizeOf(Path);
    CHECK("enospc.size_matches_written", Size >= Durable && Size <= Offset,
          "size %lld synced %lld written %lld", (long long)Size, (long long)Durable, (long long)Offset);
    {
        std::vector<uint8_t> Actual;
        bool Ok = Durable > 0 && Size >= Durable && ReadFile(Path, Actual) && (off_t)Actual.size() == Size &&
                  std::all_of(Actual.begin(), Actual.begin() + Durable, [](uint8_t Byte) { return Byte == 0x5A; });
        CHECK("enospc.data_intact", Ok, "written data unreadable after ENOSPC");
    }
    {
        int Probe = open(P("after-full").c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        int ProbeError = Probe < 0 ? errno : 0;
        if (Probe >= 0)
        {
            for (size_t i = 0; i < 2 && !ProbeError; ++i)
            {
                if (!WriteAll(Probe, Chunk.data(), Chunk.size(), i * Chunk.size()))
                    ProbeError = errno;
            }
            if (close(Probe) < 0 && !ProbeError)
                ProbeError = errno;
        }
        CHECK("enospc.write_or_close", ProbeError == ENOSPC, "full volume returned %s", strerror(ProbeError));
        unlink(P("after-full").c_str());
    }
    unlink(Path.c_str());
    sync();
    statvfs(Root.c_str(), &Released);
    CHECK("enospc.space_recovered", Released.f_bfree + 64 >= Before.f_bfree,
          "free before %llu after %llu", (unsigned long long)Before.f_bfree,
          (unsigned long long)Released.f_bfree);
    CHECK("enospc.usable_after", WriteFile(P("after.bin"), Pattern(92, 0, 100000)),
          "%s", strerror(errno));
}

/* A fixed tree another driver verifies after this one wrote it. */
static const struct PersistFile
{
    const char* Path;
    uint32_t Seed;
    size_t Size;
} PersistFiles[] = {
    {"persist/empty", 1, 0},
    {"persist/resident", 2, 400},
    {"persist/cluster", 3, 4096},
    {"persist/multi", 4, 3 * 1048576 + 17},
    {"persist/sub/inner", 5, 12345},
    {"persist/sub/deeper/leaf", 6, 70000},
    {"persist/\xc3\xa9t\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac", 7, 999},
};

static void
WritePersist()
{
    bool Ok = true;

    MakeDir("persist");
    MakeDir("persist/sub");
    MakeDir("persist/sub/deeper");
    MakeDir("persist/emptydir");
    for (const PersistFile& File : PersistFiles)
    {
        std::vector<uint8_t> Data = Pattern(File.Seed, 0, File.Size);
        Ok = Ok && WriteFile(P(File.Path), Data);
    }
    Ok = Ok && link(P("persist/cluster").c_str(), P("persist/sub/cluster-link").c_str()) == 0;
    {
        std::vector<uint8_t> Data = Pattern(8, 0, 50000);
        Ok = Ok && WriteFile(P("persist/shrunk"), Data) &&
             truncate(P("persist/shrunk").c_str(), 1000) == 0 &&
             truncate(P("persist/shrunk").c_str(), 30000) == 0;
    }
    CHECK("persist.write", Ok, "%s", strerror(errno));
}

static int
VerifyPersist()
{
    for (const PersistFile& File : PersistFiles)
    {
        std::vector<uint8_t> Actual;
        if (!ReadFile(P(File.Path), Actual) || Actual != Pattern(File.Seed, 0, File.Size))
            FAIL("verify.file", "%s differs", File.Path);
    }
    {
        std::vector<uint8_t> Actual;
        if (!ReadFile(P("persist/sub/cluster-link"), Actual) || Actual != Pattern(3, 0, 4096))
            FAIL("verify.link", "hard link content differs");
        struct stat A, B;
        if (stat(P("persist/sub/cluster-link").c_str(), &A) || stat(P("persist/cluster").c_str(), &B) ||
            A.st_ino != B.st_ino || A.st_nlink != 2)
            INFO("verify.link_inode", "ino %llu/%llu nlink %lu", (unsigned long long)A.st_ino,
                 (unsigned long long)B.st_ino, (unsigned long)A.st_nlink);
    }
    {
        std::vector<uint8_t> Actual;
        std::vector<uint8_t> Expected = Pattern(8, 0, 1000);
        Expected.resize(30000, 0);
        if (!ReadFile(P("persist/shrunk"), Actual) || Actual != Expected)
            FAIL("verify.shrunk", "shrink/regrow content differs");
    }
    {
        std::set<std::string> Names = ListNames(P("persist"));
        std::set<std::string> Expected = {"empty", "resident", "cluster", "multi", "sub",
                                          "emptydir", "shrunk",
                                          "\xc3\xa9t\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac"};
        if (Names != Expected)
            FAIL("verify.listing", "%zu names, expected %zu", Names.size(), Expected.size());
    }
    if (Failures == 0)
        PASS("verify.persist");
    return Failures == 0 ? 0 : 1;
}

int
main(int Argc, char** Argv)
{
    bool Verify = false;
    bool SkipEnospc = false;
    int First = 1;

    while (First < Argc && Argv[First][0] == '-')
    {
        if (!strcmp(Argv[First], "--verify"))
            Verify = true;
        else if (!strcmp(Argv[First], "--skip-enospc"))
            SkipEnospc = true;
        else
            break;
        First++;
    }
    if (First + 1 != Argc)
    {
        fprintf(stderr, "usage: %s [--verify] [--skip-enospc] DIRECTORY\n", Argv[0]);
        return 2;
    }
    Root = Argv[First];
    if (Verify)
        return VerifyPersist();

    TestDataSizes();
    TestEofReads();
    TestAllocationTrim();
    TestTruncateZeroes();
    TestHoleWrite();
    TestAppend();
    TestRandomOps();
    TestFallocate();
    TestNames();
    TestCase();
    TestRename();
    TestHardLinks();
    TestUnlinkOpen();
    TestErrors();
    TestBigDirectory();
    TestInterleavedScans();
    TestDeepTree();
    TestSymlinks();
    TestSymlinkChurn();
    TestRenameManyLinks();
    TestRenameLongNames();
    TestWriteDropsSetId();
    TestRewinddirSeesNewNames();
    TestResidentIndexSizes();
    TestCloseTimeNames();
    TestRemovedOpenDirectory();
    TestMovedDirectoryListing();
    TestRenameExtensionData();
    TestFullBaseRecords();
    TestSetgidDirectory();
    TestLongSymlinkTarget();
    TestModes();
    TestTimes();
    TestXattrs();
    TestMmap();
    TestMmapWriteRace();
    TestAsyncDirectWrite();
    TestCoherence();
    TestGapFill();
    TestStatfs();
    WritePersist();
    if (!SkipEnospc)
        TestEnospc();
    printf("failures: %u\n", Failures);
    return Failures == 0 ? 0 : 1;
}
