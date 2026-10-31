/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux structures of the generic system call ABI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _LX_SEMID64_DS
{
    LX_IPC64_PERM Perm;
    LONG64 Otime;
    LONG64 Ctime;
    ULONG64 Nsems;
    ULONG64 Unused3;
    ULONG64 Unused4;
} LX_SEMID64_DS, *PLX_SEMID64_DS;

C_ASSERT(sizeof(LX_SEMID64_DS) == 88);

typedef struct _LX_STAT
{
    ULONG64 Dev;
    ULONG64 Ino;
    ULONG Mode;
    ULONG Nlink;
    ULONG Uid;
    ULONG Gid;
    ULONG64 Rdev;
    ULONG64 Pad;
    LONG64 Size;
    LONG BlkSize;
    LONG Pad2;
    LONG64 Blocks;
    LX_TIMESPEC Atime;
    LX_TIMESPEC Mtime;
    LX_TIMESPEC Ctime;
    ULONG Unused[2];
} LX_STAT, *PLX_STAT;

C_ASSERT(sizeof(LX_STAT) == 128);

typedef struct _LX_EPOLL_EVENT
{
    ULONG Events;
    ULONG Pad;
    ULONG64 Data;
} LX_EPOLL_EVENT, *PLX_EPOLL_EVENT;

C_ASSERT(sizeof(LX_EPOLL_EVENT) == 16);
