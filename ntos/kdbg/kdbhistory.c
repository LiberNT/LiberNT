/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What was typed at the debugger's prompt before
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * The history is one block of characters with the commands laid end to end,
 * terminator included, and an index of where each one starts. Keeping it this
 * way means a long session of short commands remembers more of them than a
 * fixed-width table of the same size would.
 */
#define KDP_HISTORY_BUFFER_SIZE 2048
#define KDP_HISTORY_MAX_ENTRIES 64

static CHAR KdbpHistoryBuffer[KDP_HISTORY_BUFFER_SIZE];
static PCSTR KdbpHistoryEntry[KDP_HISTORY_MAX_ENTRIES];

/* Where the next command will be written, and how many are remembered */
static ULONG KdbpHistoryOffset = 0;
static ULONG KdbpHistoryCount = 0;

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Remembers one command. A command the same as the one before it is not
 * remembered twice, because a person pressing Enter twice did not mean to
 * fill the history with it.
 */
VOID
KdbpCommandHistoryAppend(
    _In_ PCSTR Command)
{
    SIZE_T Length;
    ULONG Index;

    if ((Command == NULL) || (*Command == ANSI_NULL))
        return;

    Length = strlen(Command) + 1;

    if (Length > KDP_HISTORY_BUFFER_SIZE)
        return;

    if ((KdbpHistoryCount != 0) &&
        (strcmp(KdbpHistoryEntry[(KdbpHistoryCount - 1) %
                                 KDP_HISTORY_MAX_ENTRIES], Command) == 0))
    {
        return;
    }

    /* Start over at the front when what is left will not hold it */
    if (KdbpHistoryOffset + Length > KDP_HISTORY_BUFFER_SIZE)
    {
        KdbpHistoryOffset = 0;

        /*
         * Whatever used to be at the front is about to be written over, so the
         * entries that pointed into it are dropped.
         */
        for (Index = 0; Index < KDP_HISTORY_MAX_ENTRIES; Index++)
            KdbpHistoryEntry[Index] = NULL;

        KdbpHistoryCount = 0;
    }

    RtlCopyMemory(&KdbpHistoryBuffer[KdbpHistoryOffset], Command, Length);

    KdbpHistoryEntry[KdbpHistoryCount % KDP_HISTORY_MAX_ENTRIES] =
        &KdbpHistoryBuffer[KdbpHistoryOffset];

    KdbpHistoryOffset += (ULONG)Length;
    KdbpHistoryCount++;
}

/*
 * Walks the history. NextIndex is where the caller is in it, counted back from
 * the most recent command; -1 means it has not started walking. Returns NULL
 * when there is nothing further in that direction, leaving NextIndex alone.
 */
PCSTR
KdbGetHistoryEntry(
    _Inout_ PLONG NextIndex,
    _In_ BOOLEAN Next)
{
    LONG Index = *NextIndex;
    ULONG Remembered;

    Remembered = (KdbpHistoryCount < KDP_HISTORY_MAX_ENTRIES)
                 ? KdbpHistoryCount : KDP_HISTORY_MAX_ENTRIES;

    if (Remembered == 0)
        return NULL;

    if (Next)
    {
        /* Towards the present; past the newest there is the empty line */
        if (Index < 0)
            return NULL;

        Index--;

        if (Index < 0)
        {
            *NextIndex = -1;
            return NULL;
        }
    }
    else
    {
        /* Towards the past */
        if (Index + 1 >= (LONG)Remembered)
            return NULL;

        Index++;
    }

    *NextIndex = Index;

    return KdbpHistoryEntry[(KdbpHistoryCount - 1 - (ULONG)Index) %
                            KDP_HISTORY_MAX_ENTRIES];
}

/* EOF */
