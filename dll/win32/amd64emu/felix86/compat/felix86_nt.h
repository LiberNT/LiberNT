/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host declarations the felix86 sources expect from a POSIX system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t pthread_t;

typedef struct
{
    uint64_t __val[1];
} sigset_t;

typedef struct
{
    void *ss_sp;
    int ss_flags;
    size_t ss_size;
} stack_t;

typedef struct
{
    int si_signo;
    int si_errno;
    int si_code;
    void *si_addr;
    uint64_t si_pad[13];
} siginfo_t;

typedef struct
{
    uint64_t Ra;
    uint64_t Sp;
    uint64_t S[12];
    uint64_t Fs[12];
} sigjmp_buf[1];

int Felix86NtSetJump(sigjmp_buf Buffer) __attribute__((returns_twice));
#define sigsetjmp(Buffer, SaveMask) Felix86NtSetJump(Buffer)

static inline int sigemptyset(sigset_t *Set)
{
    Set->__val[0] = 0;
    return 0;
}

int Felix86NtThreadId(void);
int Felix86NtProcessId(void);
#define gettid() Felix86NtThreadId()
#define getpid() Felix86NtProcessId()

long syscall(long Number, ...);
void sincos(double Value, double *Sine, double *Cosine);
void __riscv_flush_icache(void *Start, void *End, unsigned long Flags);

#ifndef ESRCH
#define ESRCH 3
#endif

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <string>

#define _LIBCPP_FILESYSTEM
#define _LIBCPP_FSTREAM
#define _LIBCPP___FILESYSTEM_PATH_H

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

class path {
public:
    path() = default;
    path(const char* value) : value_(value) {}
    path(const std::string& value) : value_(value) {}

    bool empty() const {
        return value_.empty();
    }

    const char* c_str() const {
        return value_.c_str();
    }

    const std::string& string() const {
        return value_;
    }

    operator std::string() const {
        return value_;
    }

    path filename() const {
        size_t separator = value_.find_last_of("/\\");
        return separator == std::string::npos ? *this : path(value_.substr(separator + 1));
    }

    std::string::const_iterator begin() const {
        return value_.begin();
    }

    std::string::const_iterator end() const {
        return value_.end();
    }

private:
    std::string value_;
};

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif
