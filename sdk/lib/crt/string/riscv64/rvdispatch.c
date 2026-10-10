/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 string and memory routines selected by the processor profile
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include <string.h>
#include "rvstring.h"

#ifdef RV_CRT_RVA23

size_t __cdecl
strlen(const char *String)
{
    return RvStrlenRva23(String);
}

int __cdecl
strcmp(const char *First, const char *Second)
{
    return RvStrcmpRva23(First, Second);
}

char * __cdecl
strcpy(char *Destination, const char *Source)
{
    return RvStrcpyRva23(Destination, Source);
}

#else

#define KF_RISCV_ZBB 0x00000040
#define KF_RISCV_V   0x00002000

typedef size_t (*RV_STRLEN)(const char *);
typedef int (*RV_STRCMP)(const char *, const char *);
typedef char *(*RV_STRCPY)(char *, const char *);
typedef void *(*RV_MEMCPY)(void *, const void *, size_t);
typedef void *(*RV_MEMSET)(void *, int, size_t);
typedef int (*RV_MEMCMP)(const void *, const void *, size_t);

void *RvMemcpyRva23(void *Destination, const void *Source, size_t Length);
void *RvMemmoveRva23(void *Destination, const void *Source, size_t Length);
void *RvMemsetRva23(void *Destination, int Value, size_t Length);
int RvMemcmpRva23(const void *First, const void *Second, size_t Length);

static ULONG
RvQueryFeatureBits(VOID)
{
    SYSTEM_CPU_INFORMATION Information;

    if (!NT_SUCCESS(NtQuerySystemInformation(SystemCpuInformation, &Information,
                                             sizeof(Information), NULL)))
        return 0;
    return Information.ProcessorFeatureBits;
}

static size_t RvStrlenSelect(const char *String);
static int RvStrcmpSelect(const char *First, const char *Second);
static char *RvStrcpySelect(char *Destination, const char *Source);
static void *RvMemcpySelect(void *Destination, const void *Source, size_t Length);
static void *RvMemsetSelect(void *Destination, int Value, size_t Length);
static void *RvMemcpyScalar(void *Destination, const void *Source, size_t Length);
static void *RvMemmoveSelect(void *Destination, const void *Source, size_t Length);
static void *RvMemmoveScalar(void *Destination, const void *Source, size_t Length);
static void *RvMemsetScalar(void *Destination, int Value, size_t Length);
static int RvMemcmpSelect(const void *First, const void *Second, size_t Length);
static int RvMemcmpScalar20(const void *First, const void *Second, size_t Length);
static int RvMemcmpScalar22(const void *First, const void *Second, size_t Length);

static RV_STRLEN volatile RvStrlen = RvStrlenSelect;
static RV_STRCMP volatile RvStrcmp = RvStrcmpSelect;
static RV_STRCPY volatile RvStrcpy = RvStrcpySelect;
static RV_MEMCPY volatile RvMemcpy = RvMemcpySelect;
static RV_MEMCPY volatile RvMemmove = RvMemmoveSelect;
static RV_MEMSET volatile RvMemset = RvMemsetSelect;
static RV_MEMCMP volatile RvMemcmp = RvMemcmpSelect;

static VOID
RvSelect(VOID)
{
    ULONG FeatureBits = RvQueryFeatureBits();

    if ((FeatureBits & (KF_RISCV_V | KF_RISCV_ZBB)) == (KF_RISCV_V | KF_RISCV_ZBB))
    {
        RvMemcmp = RvMemcmpRva23;
        RvMemset = RvMemsetRva23;
        RvMemcpy = RvMemcpyRva23;
        RvMemmove = RvMemmoveRva23;
        RvStrcpy = RvStrcpyRva23;
        RvStrcmp = RvStrcmpRva23;
        RvStrlen = RvStrlenRva23;
        return;
    }

    RvMemset = RvMemsetScalar;
    RvMemcpy = RvMemcpyScalar;
    RvMemmove = RvMemmoveScalar;
    if (FeatureBits & KF_RISCV_ZBB)
    {
        RvMemcmp = RvMemcmpScalar22;
        RvStrcpy = RvStrcpyRva22;
        RvStrcmp = RvStrcmpRva22;
        RvStrlen = RvStrlenRva22;
    }
    else
    {
        RvMemcmp = RvMemcmpScalar20;
        RvStrcpy = RvStrcpyRva20;
        RvStrcmp = RvStrcmpRva20;
        RvStrlen = RvStrlenRva20;
    }
}

static size_t
RvStrlenSelect(const char *String)
{
    RvSelect();
    return RvStrlen(String);
}

static int
RvStrcmpSelect(const char *First, const char *Second)
{
    RvSelect();
    return RvStrcmp(First, Second);
}

static char *
RvStrcpySelect(char *Destination, const char *Source)
{
    RvSelect();
    return RvStrcpy(Destination, Source);
}

size_t __cdecl
strlen(const char *String)
{
    return RvStrlen(String);
}

int __cdecl
strcmp(const char *First, const char *Second)
{
    return RvStrcmp(First, Second);
}

char * __cdecl
strcpy(char *Destination, const char *Source)
{
    return RvStrcpy(Destination, Source);
}

static void *
RvMemcpySelect(void *Destination, const void *Source, size_t Length)
{
    RvSelect();
    return RvMemcpy(Destination, Source, Length);
}

static void *
RvMemsetSelect(void *Destination, int Value, size_t Length)
{
    RvSelect();
    return RvMemset(Destination, Value, Length);
}

static int
RvMemcmpSelect(const void *First, const void *Second, size_t Length)
{
    RvSelect();
    return RvMemcmp(First, Second, Length);
}

static int
RvMemcmpScalar20(const void *First, const void *Second, size_t Length)
{
    int Result = RvMemcmpRva20(First, Second, Length);

    return (Result > 0) - (Result < 0);
}

static int
RvMemcmpScalar22(const void *First, const void *Second, size_t Length)
{
    int Result = RvMemcmpRva22(First, Second, Length);

    return (Result > 0) - (Result < 0);
}

static void *
RvMemcpyScalar(void *Destination, const void *Source, size_t Length)
{
    return RvMemcpyRva20(Destination, Source, Length);
}

static void *
RvMemsetScalar(void *Destination, int Value, size_t Length)
{
    return RvMemsetRva20(Destination, Value, Length);
}

static void *
RvMemmoveSelect(void *Destination, const void *Source, size_t Length)
{
    RvSelect();
    return RvMemmove(Destination, Source, Length);
}

static void *
RvMemmoveScalar(void *Destination, const void *Source, size_t Length)
{
    return RvMemmoveRva20(Destination, Source, Length);
}

void * __cdecl
memcpy(void *Destination, const void *Source, size_t Length)
{
    return RvMemcpy(Destination, Source, Length);
}

void * __cdecl
memmove(void *Destination, const void *Source, size_t Length)
{
    return RvMemmove(Destination, Source, Length);
}

void * __cdecl
memset(void *Destination, int Value, size_t Length)
{
    return RvMemset(Destination, Value, Length);
}

int __cdecl
memcmp(const void *First, const void *Second, size_t Length)
{
    return RvMemcmp(First, Second, Length);
}

#endif
