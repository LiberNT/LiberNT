/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rewriting a hive file without the gaps its history left in it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Builds a fresh hive holding exactly what this one holds, in cells packed
 * one after another, and writes it over the hive's own file. The hive goes on
 * running from what it has in memory; the packed form is what the next boot
 * reads, in the same way a replaced hive file takes hold on a restart.
 */
NTSTATUS
NTAPI
CmpCompactHive(
    _In_ PCMHIVE Hive)
{
    HCELL_INDEX RootCell = HCELL_NIL;
    PCM_KEY_NODE Node;
    PCMHIVE Packed;
    NTSTATUS Status;

    if (CmpNoWrite) return STATUS_SUCCESS;
    if (Hive->Hive.HiveFlags & HIVE_VOLATILE) return STATUS_SUCCESS;
    if (Hive->Hive.ReadOnly) return STATUS_ACCESS_DENIED;
    if (Hive->FileHandles[HFILE_TYPE_PRIMARY] == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Hive->Hive.BaseBlock == NULL) return STATUS_INVALID_PARAMETER;

    Status = CmpInitializeHive(&Packed,
                               HINIT_CREATE,
                               HIVE_VOLATILE,
                               HFILE_TYPE_PRIMARY,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               0);
    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpDeepCopyKey(&Hive->Hive,
                            Hive->Hive.BaseBlock->RootCell,
                            &Packed->Hive,
                            Stable,
                            &RootCell);
    if (!NT_SUCCESS(Status) || RootCell == HCELL_NIL)
    {
        CmpDestroyHive(Packed);
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    }

    Node = (PCM_KEY_NODE)HvGetCell(&Packed->Hive, RootCell);

    if (Node != NULL)
    {
        Node->Flags |= KEY_HIVE_ENTRY | KEY_NO_DELETE;
        Node->Parent = HCELL_NIL;
        HvMarkCellDirty(&Packed->Hive, RootCell, FALSE);
        HvReleaseCell(&Packed->Hive, RootCell);
    }

    Packed->Hive.BaseBlock->RootCell = RootCell;

    CmpLockHiveFlusherExclusive(Hive);

    Packed->Hive.HiveFlags &= ~HIVE_VOLATILE;
    Packed->FileHandles[HFILE_TYPE_PRIMARY] =
        Hive->FileHandles[HFILE_TYPE_PRIMARY];

    Status = HvWriteHive(&Packed->Hive) ? STATUS_SUCCESS
                                        : STATUS_REGISTRY_IO_FAILED;

    Packed->FileHandles[HFILE_TYPE_PRIMARY] = NULL;
    Packed->Hive.HiveFlags |= HIVE_VOLATILE;

    if (NT_SUCCESS(Status))
    {
        Hive->Hive.DirtyCount = 0;
        Hive->PrimaryFileSizeBeforeLastFlush = 0;
    }

    CmpUnlockHiveFlusher(Hive);

    CmpDestroyHive(Packed);
    return Status;
}

/*
 * Packs the hives behind a set of keys, each hive only once however many of
 * the keys live in it.
 */
NTSTATUS
NTAPI
CmpCompactHivesOfKeys(
    _In_ ULONG Count,
    _In_reads_(Count) PCM_KEY_BODY *KeyBodies)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PCMHIVE Hive;
    ULONG i;
    ULONG j;

    for (i = 0; i < Count; i++)
    {
        if (KeyBodies[i] == NULL) continue;
        if (KeyBodies[i]->KeyControlBlock == NULL) continue;

        Hive = CONTAINING_RECORD(KeyBodies[i]->KeyControlBlock->KeyHive,
                                 CMHIVE,
                                 Hive);

        for (j = 0; j < i; j++)
        {
            if (KeyBodies[j] == NULL) continue;
            if (KeyBodies[j]->KeyControlBlock == NULL) continue;

            if (KeyBodies[j]->KeyControlBlock->KeyHive ==
                KeyBodies[i]->KeyControlBlock->KeyHive)
            {
                break;
            }
        }

        if (j != i) continue;

        Status = CmpCompactHive(Hive);
        if (!NT_SUCCESS(Status)) break;
    }

    return Status;
}
