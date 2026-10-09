/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     _recalloc on the heap of the imported msvcrt.dll
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define _CRTIMP

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

void* __cdecl _recalloc(void* Block, size_t Count, size_t Size)
{
    size_t NewSize;
    size_t OldSize;
    void* NewBlock;

    if ((Size != 0) && (Count > ((size_t)-1) / Size))
    {
        errno = ENOMEM;
        return NULL;
    }

    if (Block == NULL)
    {
        return calloc(Count, Size);
    }

    NewSize = Count * Size;
    OldSize = _msize(Block);
    NewBlock = realloc(Block, NewSize);
    if ((NewBlock != NULL) && (NewSize > OldSize))
    {
        memset((char*)NewBlock + OldSize, 0, NewSize - OldSize);
    }

    return NewBlock;
}

#ifdef _M_IX86
const void* const _imp___recalloc = _recalloc;
#else
const void* const __imp__recalloc = _recalloc;
#endif
