/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Flattened device tree builder for unit tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdarg.h>

typedef struct _FDT_BUILDER
{
    UCHAR Structure[4096];
    ULONG StructureSize;
    CHAR Strings[1024];
    ULONG StringsSize;
    UCHAR Blob[8192];
    ULONG BlobSize;
} FDT_BUILDER;

static FDT_BUILDER Builder;

static
VOID
PutBigEndian32(PUCHAR Out, ULONG Value)
{
    Out[0] = (UCHAR)(Value >> 24);
    Out[1] = (UCHAR)(Value >> 16);
    Out[2] = (UCHAR)(Value >> 8);
    Out[3] = (UCHAR)Value;
}

static
VOID
FdtToken(ULONG Token)
{
    PutBigEndian32(Builder.Structure + Builder.StructureSize, Token);
    Builder.StructureSize += 4;
}

static
VOID
FdtBytes(const VOID *Data, ULONG Length)
{
    if (Length)
        memcpy(Builder.Structure + Builder.StructureSize, Data, Length);
    Builder.StructureSize += Length;
    while (Builder.StructureSize & 3)
        Builder.Structure[Builder.StructureSize++] = 0;
}

static
VOID
FdtBeginNode(const CHAR *Name)
{
    FdtToken(1);
    FdtBytes(Name, (ULONG)strlen(Name) + 1);
}

static
VOID
FdtEndNode(VOID)
{
    FdtToken(2);
}

static
VOID
FdtProperty(const CHAR *Name, const VOID *Data, ULONG Length)
{
    ULONG Offset = Builder.StringsSize;

    memcpy(Builder.Strings + Builder.StringsSize, Name, strlen(Name) + 1);
    Builder.StringsSize += (ULONG)strlen(Name) + 1;
    FdtToken(3);
    FdtToken(Length);
    FdtToken(Offset);
    FdtBytes(Data, Length);
}

static
VOID
FdtCells(const CHAR *Name, ULONG Count, ...)
{
    UCHAR Data[256];
    va_list Arguments;
    ULONG Index;

    if (Count > sizeof(Data) / 4)
        Count = sizeof(Data) / 4;
    va_start(Arguments, Count);
    for (Index = 0; Index < Count; ++Index)
        PutBigEndian32(Data + 4 * Index, va_arg(Arguments, ULONG));
    va_end(Arguments);
    FdtProperty(Name, Data, 4 * Count);
}

static
VOID
FdtStrings(const CHAR *Name, const CHAR *List, ULONG Length)
{
    FdtProperty(Name, List, Length);
}

static
VOID
FdtFinish(VOID)
{
    ULONG StructureOffset = 40 + 16;

    FdtToken(9);
    memset(Builder.Blob, 0, sizeof(Builder.Blob));
    memcpy(Builder.Blob + StructureOffset, Builder.Structure, Builder.StructureSize);
    memcpy(Builder.Blob + StructureOffset + Builder.StructureSize, Builder.Strings, Builder.StringsSize);
    Builder.BlobSize = StructureOffset + Builder.StructureSize + Builder.StringsSize;
    PutBigEndian32(Builder.Blob + 0, 0xd00dfeed);
    PutBigEndian32(Builder.Blob + 4, Builder.BlobSize);
    PutBigEndian32(Builder.Blob + 8, StructureOffset);
    PutBigEndian32(Builder.Blob + 12, StructureOffset + Builder.StructureSize);
    PutBigEndian32(Builder.Blob + 16, 40);
    PutBigEndian32(Builder.Blob + 20, 17);
    PutBigEndian32(Builder.Blob + 24, 16);
    PutBigEndian32(Builder.Blob + 28, 0);
    PutBigEndian32(Builder.Blob + 32, Builder.StringsSize);
    PutBigEndian32(Builder.Blob + 36, Builder.StructureSize);
}

#define LIST(Text) Text, sizeof(Text)
