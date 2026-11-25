/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Extracts one image of a WIM through WimBootExtract into a directory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wimboot.h"

typedef struct _EXTRACT
{
    const char* Root;
    unsigned Files;
    unsigned Directories;
    unsigned Dated;
} EXTRACT;

static void* Allocate(size_t Size, void* Context)
{
    (void)Context;
    return malloc(Size);
}

static void Release(void* Memory, void* Context)
{
    (void)Context;
    free(Memory);
}

static int ReadAt(void* Context, uint64_t Offset, void* Buffer, size_t Size)
{
    int File = *(int*)Context;
    size_t Done = 0;
    ssize_t Read;

    while (Done < Size)
    {
        Read = pread(File, (char*)Buffer + Done, Size - Done, (off_t)(Offset + Done));
        if (Read <= 0)
            return -1;
        Done += (size_t)Read;
    }
    return 0;
}

static int Write(void* Context, const WIMBOOT_ENTRY* Entry, const uint8_t* Data)
{
    EXTRACT* Extract = (EXTRACT*)Context;
    char Path[4096];
    uint64_t Done = 0;
    ssize_t Written;
    int File;

    if (snprintf(Path, sizeof(Path), "%s/%s", Extract->Root, Entry->Path) >= (int)sizeof(Path))
        return 1;
    Extract->Dated += Entry->LastWriteTime != 0 && Entry->CreationTime != 0;
    if (Entry->IsDirectory)
    {
        Extract->Directories++;
        return mkdir(Path, 0755) != 0 && errno != EEXIST;
    }

    if ((Entry->Size != 0) != (Data != NULL))
        return 1;
    File = open(Path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (File < 0)
        return 1;
    while (Done < Entry->Size)
    {
        Written = write(File, Data + Done, (size_t)(Entry->Size - Done));
        if (Written <= 0)
        {
            close(File);
            return 1;
        }
        Done += (uint64_t)Written;
    }
    Extract->Files++;
    return close(File) != 0;
}

int main(int argc, char** argv)
{
    WimCoreAllocator Allocator = { Allocate, Release, NULL };
    WIMBOOT_IMAGE Image;
    WimCoreCtx Wim;
    EXTRACT Extract;
    struct stat Info;
    int File, Result;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s WIM INDEX DIRECTORY\n", argv[0]);
        return 2;
    }

    File = open(argv[1], O_RDONLY);
    if (File < 0 || fstat(File, &Info) != 0)
        return 2;

    wimcore_init(&Wim);
    if (wimcore_open(&Wim, &Allocator, ReadAt, &File, (uint64_t)Info.st_size) != 0)
        return 3;
    Result = WimBootOpenImage(&Wim, (uint32_t)strtoul(argv[2], NULL, 10), &Image);
    if (Result != WIMBOOT_OK)
    {
        printf("open %d\n", Result);
        wimcore_close(&Wim);
        return 4;
    }

    memset(&Extract, 0, sizeof(Extract));
    Extract.Root = argv[3];
    Result = WimBootExtract(&Wim, &Image, Write, &Extract);
    printf("image %u: result %d, %u files, %u directories, %u dated\n",
           Image.ImageIndex, Result, Extract.Files, Extract.Directories, Extract.Dated);
    wimcore_close(&Wim);
    close(File);
    return Result == WIMBOOT_OK && Extract.Dated == Extract.Files + Extract.Directories ? 0 : 5;
}
