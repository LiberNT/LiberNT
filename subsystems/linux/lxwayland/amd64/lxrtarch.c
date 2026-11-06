/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86-64 entry point and system calls of the Wayland compositor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxrt.h"

typedef struct __attribute__((packed)) _LX_X64_EPOLL_EVENT
{
    uint32_t Events;
    uint64_t Data;
} LX_X64_EPOLL_EVENT;

__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"
    "    mov %rsp, %rdi\n"
    "    and $-16, %rsp\n"
    "    call LxStart\n"
    "    ud2\n");

static
long
LxX64Number(long Number)
{
    switch (Number)
    {
        case LX_NR_EPOLL_CREATE1:
            return 291;
        case LX_NR_EPOLL_CTL:
            return 233;
        case LX_NR_EPOLL_PWAIT:
            return 281;
        case LX_NR_FCNTL:
            return 72;
        case LX_NR_IOCTL:
            return 16;
        case LX_NR_MKDIRAT:
            return 258;
        case LX_NR_UNLINKAT:
            return 263;
        case LX_NR_FCHMODAT:
            return 268;
        case LX_NR_OPENAT:
            return 257;
        case LX_NR_CLOSE:
            return 3;
        case LX_NR_PIPE2:
            return 293;
        case LX_NR_LSEEK:
            return 8;
        case LX_NR_READ:
            return 0;
        case LX_NR_WRITE:
            return 1;
        case LX_NR_EXIT_GROUP:
            return 231;
        case LX_NR_CLOCK_GETTIME:
            return 228;
        case LX_NR_RT_SIGACTION:
            return 13;
        case LX_NR_SOCKET:
            return 41;
        case LX_NR_BIND:
            return 49;
        case LX_NR_LISTEN:
            return 50;
        case LX_NR_GETSOCKOPT:
            return 55;
        case LX_NR_SENDMSG:
            return 46;
        case LX_NR_RECVMSG:
            return 47;
        case LX_NR_MUNMAP:
            return 11;
        case LX_NR_MMAP:
            return 9;
        case LX_NR_ACCEPT4:
            return 288;
        case LX_NR_MEMFD_CREATE:
            return 319;
        default:
            return -1;
    }
}

static
long
LxX64Syscall(long Number, long A0, long A1, long A2, long A3, long A4, long A5)
{
    register long R10 __asm__("r10") = A3;
    register long R8 __asm__("r8") = A4;
    register long R9 __asm__("r9") = A5;
    long Result;

    __asm__ __volatile__("syscall"
                         : "=a"(Result)
                         : "a"(Number), "D"(A0), "S"(A1), "d"(A2), "r"(R10), "r"(R8), "r"(R9)
                         : "rcx", "r11", "memory");
    return Result;
}

long
LxSyscall(long Number, long A0, long A1, long A2, long A3, long A4, long A5)
{
    LX_X64_EPOLL_EVENT Packed[64];
    LX_EPOLL_EVENT *Events = (LX_EPOLL_EVENT *)A3;
    long Result, Index;

    if ((Number == LX_NR_EPOLL_CTL) && (Events != NULL))
    {
        Packed[0].Events = Events->Events;
        Packed[0].Data = Events->Data;
        return LxX64Syscall(LxX64Number(Number), A0, A1, A2, (long)Packed, A4, A5);
    }
    if (Number == LX_NR_EPOLL_PWAIT)
    {
        Events = (LX_EPOLL_EVENT *)A1;
        if (A2 > 64)
            A2 = 64;
        Result = LxX64Syscall(LxX64Number(Number), A0, (long)Packed, A2, A3, A4, A5);
        for (Index = 0; Index < Result; Index++)
        {
            Events[Index].Events = Packed[Index].Events;
            Events[Index].Data = Packed[Index].Data;
        }
        return Result;
    }
    return LxX64Syscall(LxX64Number(Number), A0, A1, A2, A3, A4, A5);
}
