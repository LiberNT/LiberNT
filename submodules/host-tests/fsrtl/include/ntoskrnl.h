/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimal kernel definitions for building the FsRtl name matchers on the host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define _In_
#define _In_opt_
#define _Out_
#define _Inout_
#define _In_reads_bytes_(x)
#define _In_reads_opt_(x)
#define NTAPI
#define CODE_SEG(x)
#define PAGED_CODE()
#define TRUE 1
#define FALSE 0
#define UNICODE_NULL ((WCHAR)0)

typedef void VOID, *PVOID;
typedef unsigned char UCHAR, BOOLEAN, *PUCHAR, *PBOOLEAN;
typedef char CHAR, *PCHAR;
typedef unsigned short USHORT, WCHAR, *PWCHAR, *PWSTR;
typedef const WCHAR *PCWSTR, *PCWCH;
typedef int32_t LONG, NTSTATUS;
typedef uint32_t ULONG, *PULONG;
typedef size_t SIZE_T;
typedef int POOL_TYPE;

#define PagedPool 1
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017)

typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

typedef struct _STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} ANSI_STRING, *PANSI_STRING;

#define DOS_STAR ((WCHAR)'<')
#define DOS_QM ((WCHAR)'>')
#define DOS_DOT ((WCHAR)'"')
#define ANSI_DOS_STAR ('<')
#define ANSI_DOS_QM ('>')
#define ANSI_DOS_DOT ('"')

#define FsRtlIsLeadDbcsCharacter(c) FALSE
#define FsRtlIsAnsiCharacterWild(c) ((c) == '*' || (c) == '?' || (c) == '<' || (c) == '>' || (c) == '"')
#define FsRtlIsAnsiCharacterLegalFat(c, w) (((c) >= 0x20 && !strchr("\"*+,/:;<=>?[\\]|", (c))) || ((w) && FsRtlIsAnsiCharacterWild(c)))
#define FsRtlIsAnsiCharacterLegalHpfs(c, w) (((c) >= 0x20 && !strchr("\"*/:<>?\\|", (c))) || ((w) && FsRtlIsAnsiCharacterWild(c)))

#define RtlZeroMemory(d, l) memset((d), 0, (l))
#define ASSERT(x) do { if (!(x)) abort(); } while (0)

static inline SIZE_T RtlCompareMemory(const void *A, const void *B, SIZE_T Length)
{
    SIZE_T Index = 0;

    while (Index < Length && ((const UCHAR *)A)[Index] == ((const UCHAR *)B)[Index])
        Index++;
    return Index;
}

static inline WCHAR RtlUpcaseUnicodeChar(WCHAR Character)
{
    return (Character >= 'a' && Character <= 'z') ? (WCHAR)(Character - 'a' + 'A') : Character;
}

static inline LONG RtlCompareUnicodeString(PCUNICODE_STRING A, PCUNICODE_STRING B, BOOLEAN IgnoreCase)
{
    USHORT Index;
    USHORT Length = A->Length < B->Length ? A->Length : B->Length;

    for (Index = 0; Index < Length / sizeof(WCHAR); Index++)
    {
        WCHAR Left = IgnoreCase ? RtlUpcaseUnicodeChar(A->Buffer[Index]) : A->Buffer[Index];
        WCHAR Right = IgnoreCase ? RtlUpcaseUnicodeChar(B->Buffer[Index]) : B->Buffer[Index];

        if (Left != Right)
            return (LONG)Left - (LONG)Right;
    }
    return (LONG)A->Length - (LONG)B->Length;
}

static inline PVOID ExAllocatePoolWithTag(POOL_TYPE Type, SIZE_T Size, ULONG Tag)
{
    (void)Type;
    (void)Tag;
    return malloc(Size);
}

static inline VOID ExFreePoolWithTag(PVOID Block, ULONG Tag)
{
    (void)Tag;
    free(Block);
}

static inline VOID ExRaiseStatus(NTSTATUS Status)
{
    (void)Status;
    abort();
}
