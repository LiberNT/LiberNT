/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the ISO 9660 directory name tests
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */

#ifndef FREELDR_ISO_TEST_SHIM_H
#define FREELDR_ISO_TEST_SHIM_H

#include <stddef.h>
#include <stdint.h>

/*
 * The translation unit under test takes the FreeLoader types from
 * <freeldr.h>, which is a target header. The host build supplies the narrow
 * subset the routine uses and neutralises that header through its guard.
 */
#define __FREELDR_H

#define _In_
#define _In_opt_
#define _Inout_
#define _Out_
#define _Out_opt_
#define _Out_writes_(size)

#define FIELD_OFFSET(type, field) offsetof(type, field)

#define MAX_PATH 260

#define TRUE 1
#define FALSE 0

typedef void VOID;
typedef void *PVOID;
typedef char CHAR, *PCHAR;
typedef char CCHAR;
typedef const char *PCSTR, *PCCHAR;
typedef int32_t LONG;
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN, *PBOOLEAN;
typedef uint32_t ULONG, *PULONG, ARC_STATUS, CONFIGURATION_TYPE;
typedef uint64_t ULONGLONG;
typedef unsigned short USHORT;
typedef size_t SIZE_T;

struct tagDEVVTBL;
typedef struct tagDEVVTBL DEVVTBL;

struct _PARTITION_INFORMATION;
typedef struct _PARTITION_INFORMATION *PPARTITION_INFORMATION;

#define RTL_FIELD_SIZE(type, field) sizeof(((type *)0)->field)

/* arc/arc.h's file information, which iso.h measures its name after */
typedef struct _LARGE_INTEGER
{
    uint32_t LowPart;
    int32_t HighPart;
} LARGE_INTEGER;

typedef struct _FILEINFORMATION
{
    LARGE_INTEGER StartingAddress;
    LARGE_INTEGER EndingAddress;
    LARGE_INTEGER CurrentAddress;
    CONFIGURATION_TYPE Type;
    ULONG FileNameLength;
    UCHAR Attributes;
    CHAR FileName[32];
} FILEINFORMATION;

#endif
