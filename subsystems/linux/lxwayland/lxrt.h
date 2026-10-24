/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Freestanding Linux runtime of the Wayland compositor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LX_NR_EPOLL_CREATE1     20
#define LX_NR_EPOLL_CTL         21
#define LX_NR_EPOLL_PWAIT       22
#define LX_NR_FCNTL             25
#define LX_NR_MKDIRAT           34
#define LX_NR_UNLINKAT          35
#define LX_NR_FCHMODAT          53
#define LX_NR_OPENAT            56
#define LX_NR_CLOSE             57
#define LX_NR_PIPE2             59
#define LX_NR_READ              63
#define LX_NR_WRITE             64
#define LX_NR_EXIT_GROUP        94
#define LX_NR_CLOCK_GETTIME     113
#define LX_NR_RT_SIGACTION      134
#define LX_NR_SOCKET            198
#define LX_NR_BIND              200
#define LX_NR_LISTEN            201
#define LX_NR_GETSOCKOPT        209
#define LX_NR_SENDMSG           211
#define LX_NR_RECVMSG           212
#define LX_NR_MUNMAP            215
#define LX_NR_MMAP              222
#define LX_NR_ACCEPT4           242
#define LX_NR_MEMFD_CREATE      279

#define LX_ENOENT               2
#define LX_EINTR                4
#define LX_EAGAIN               11
#define LX_EEXIST               17
#define LX_ENOTCONN             107

#define LX_O_RDONLY             0
#define LX_O_RDWR               02
#define LX_O_NONBLOCK           04000
#define LX_O_CLOEXEC            02000000
#define LX_AT_FDCWD             (-100)
#define LX_F_GETFL              3
#define LX_F_SETFL              4
#define LX_MFD_CLOEXEC          1

#define LX_PROT_READ            1
#define LX_PROT_WRITE           2
#define LX_MAP_SHARED           0x01
#define LX_MAP_PRIVATE          0x02
#define LX_MAP_ANONYMOUS        0x20

#define LX_EPOLLIN              0x001
#define LX_EPOLLOUT             0x004
#define LX_EPOLLERR             0x008
#define LX_EPOLLHUP             0x010
#define LX_EPOLL_CTL_ADD        1
#define LX_EPOLL_CTL_DEL        2
#define LX_EPOLL_CTL_MOD        3

#define LX_AF_UNIX              1
#define LX_SOCK_STREAM          1
#define LX_SOCK_NONBLOCK        04000
#define LX_SOCK_CLOEXEC         02000000
#define LX_SOL_SOCKET           1
#define LX_SCM_RIGHTS           1
#define LX_SO_PEERCRED          17
#define LX_MSG_DONTWAIT         0x40
#define LX_MSG_NOSIGNAL         0x4000
#define LX_MSG_CMSG_CLOEXEC     0x40000000

#define LX_CLOCK_MONOTONIC      1
#define LX_SIGPIPE              13

typedef struct _LX_IOVEC
{
    void *Base;
    size_t Length;
} LX_IOVEC;

typedef struct _LX_MSGHDR
{
    void *Name;
    uint32_t NameLength;
    LX_IOVEC *Iov;
    size_t IovLength;
    void *Control;
    size_t ControlLength;
    uint32_t Flags;
} LX_MSGHDR;

typedef struct _LX_CMSGHDR
{
    size_t Length;
    int32_t Level;
    int32_t Type;
} LX_CMSGHDR;

#define LX_CMSG_ALIGN(Length)   (((Length) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define LX_CMSG_SPACE(Length)   (LX_CMSG_ALIGN(sizeof(LX_CMSGHDR)) + LX_CMSG_ALIGN(Length))
#define LX_CMSG_LEN(Length)     (LX_CMSG_ALIGN(sizeof(LX_CMSGHDR)) + (Length))
#define LX_CMSG_DATA(Header)    ((unsigned char *)(Header) + LX_CMSG_ALIGN(sizeof(LX_CMSGHDR)))

typedef struct _LX_SOCKADDR_UN
{
    uint16_t Family;
    char Path[108];
} LX_SOCKADDR_UN;

typedef struct _LX_UCRED
{
    int32_t Pid;
    uint32_t Uid;
    uint32_t Gid;
} LX_UCRED;

typedef struct _LX_EPOLL_EVENT
{
    uint32_t Events;
    uint64_t Data;
} LX_EPOLL_EVENT;

typedef struct _LX_TIMESPEC
{
    int64_t Seconds;
    int64_t Nanoseconds;
} LX_TIMESPEC;

long LxSyscall(long Number, long A0, long A1, long A2, long A3, long A4, long A5);

void *memcpy(void *Destination, const void *Source, size_t Length);
void *memmove(void *Destination, const void *Source, size_t Length);
void *memset(void *Destination, int Value, size_t Length);
int memcmp(const void *Left, const void *Right, size_t Length);
size_t strlen(const char *String);
int strcmp(const char *Left, const char *Right);

void *LxAlloc(size_t Size);
void *LxAllocZero(size_t Size);
void *LxRealloc(void *Block, size_t Size);
void LxFree(void *Block);
char *LxStrDup(const char *String);

void LxLog(const char *Format, ...);
uint64_t LxMonotonicMs(void);
void LxIgnoreSigpipe(void);
__attribute__((noreturn)) void LxExit(int Code);

int LxMain(void);
