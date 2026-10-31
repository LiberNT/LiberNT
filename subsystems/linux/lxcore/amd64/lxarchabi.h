/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86-64 Linux file flags and structures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define LX_O_DIRECTORY      0200000
#define LX_O_NOFOLLOW       0400000
#define LX_O_DIRECT         040000
#define LX_O_LARGEFILE      0100000

typedef struct _LX_SEMID64_DS
{
    LX_IPC64_PERM Perm;
    LONG64 Otime;
    ULONG64 Unused1;
    LONG64 Ctime;
    ULONG64 Unused2;
    ULONG64 Nsems;
    ULONG64 Unused3;
    ULONG64 Unused4;
} LX_SEMID64_DS, *PLX_SEMID64_DS;

C_ASSERT(sizeof(LX_SEMID64_DS) == 104);

typedef struct _LX_STAT
{
    ULONG64 Dev;
    ULONG64 Ino;
    ULONG64 Nlink;
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    ULONG Pad;
    ULONG64 Rdev;
    LONG64 Size;
    LONG64 BlkSize;
    LONG64 Blocks;
    LX_TIMESPEC Atime;
    LX_TIMESPEC Mtime;
    LX_TIMESPEC Ctime;
    LONG64 Unused[3];
} LX_STAT, *PLX_STAT;

C_ASSERT(sizeof(LX_STAT) == 144);

#pragma pack(push, 4)
typedef struct _LX_EPOLL_EVENT
{
    ULONG Events;
    ULONG64 Data;
} LX_EPOLL_EVENT, *PLX_EPOLL_EVENT;
#pragma pack(pop)

C_ASSERT(sizeof(LX_EPOLL_EVENT) == 12);
