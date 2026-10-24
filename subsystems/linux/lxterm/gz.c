/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Streaming decoder of gzip files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>

#include <zlib.h>

#include "lxinstall.h"

#define LX_GZ_INPUT_SIZE    (256 * 1024)

struct _LX_GZ
{
    HANDLE File;
    z_stream Stream;
    PUCHAR Input;
    ULONG64 Consumed;
    BOOLEAN Finished;
    BOOLEAN Failed;
};

static
BOOL
LxGzFill(
    _Inout_ PLX_GZ Gz)
{
    DWORD Read;

    if (Gz->Stream.avail_in != 0)
        return TRUE;
    if (!ReadFile(Gz->File, Gz->Input, LX_GZ_INPUT_SIZE, &Read, NULL) || (Read == 0))
        return FALSE;
    Gz->Stream.next_in = Gz->Input;
    Gz->Stream.avail_in = Read;
    Gz->Consumed += Read;
    return TRUE;
}

PLX_GZ
LxGzOpen(
    _In_ HANDLE File)
{
    PLX_GZ Gz = calloc(1, sizeof(*Gz));

    if (Gz == NULL)
        return NULL;
    Gz->Input = malloc(LX_GZ_INPUT_SIZE);
    if ((Gz->Input == NULL) || (inflateInit2(&Gz->Stream, 16 + MAX_WBITS) != Z_OK))
    {
        free(Gz->Input);
        free(Gz);
        return NULL;
    }
    Gz->File = File;
    return Gz;
}

LONG
LxGzRead(
    _Inout_ PLX_GZ Gz,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    Gz->Stream.next_out = Buffer;
    Gz->Stream.avail_out = Size;
    while ((Gz->Stream.avail_out != 0) && !Gz->Finished && !Gz->Failed)
    {
        int Result;

        if (!LxGzFill(Gz))
        {
            Gz->Failed = TRUE;
            break;
        }
        Result = inflate(&Gz->Stream, Z_NO_FLUSH);
        if (Result == Z_STREAM_END)
        {
            if (!LxGzFill(Gz))
                Gz->Finished = TRUE;
            else if (inflateReset(&Gz->Stream) != Z_OK)
                Gz->Failed = TRUE;
        }
        else if ((Result != Z_OK) && (Result != Z_BUF_ERROR))
        {
            Gz->Failed = TRUE;
        }
    }
    if (Gz->Failed)
        return -1;
    return (LONG)(Size - Gz->Stream.avail_out);
}

ULONG64
LxGzPosition(
    _In_ PLX_GZ Gz)
{
    return Gz->Consumed - Gz->Stream.avail_in;
}

VOID
LxGzClose(
    _In_ PLX_GZ Gz)
{
    inflateEnd(&Gz->Stream);
    free(Gz->Input);
    free(Gz);
}
