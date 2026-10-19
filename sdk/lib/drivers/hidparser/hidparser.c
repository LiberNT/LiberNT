/*
 * PROJECT:     ReactOS HID Parser Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/drivers/hidparser/hidparser.c
 * PURPOSE:     HID Parser
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 *              Ahmed ARIF (arif.ing@outlook.com)
 */

#include "hidp.h"

#define NDEBUG
#include <debug.h>

HIDAPI
NTSTATUS
NTAPI
HidParser_UsageListDifference(
  IN PUSAGE  PreviousUsageList,
  IN PUSAGE  CurrentUsageList,
  OUT PUSAGE  BreakUsageList,
  OUT PUSAGE  MakeUsageList,
  IN ULONG  UsageListLength)
{
    ULONG Index, SubIndex, bFound, BreakUsageIndex = 0, MakeUsageIndex = 0;
    USAGE CurrentUsage, Usage;

    if (UsageListLength)
    {
        Index = 0;
        do
        {
            /* get current usage */
            CurrentUsage = PreviousUsageList[Index];

            /* is the end of list reached? */
            if (!CurrentUsage)
                break;

            /* start searching in current usage list */
            SubIndex = 0;
            bFound = FALSE;
            do
            {
                /* get usage of current list */
                Usage = CurrentUsageList[SubIndex];

                /* end of list reached? */
                if (!Usage)
                    break;

                /* check if it matches the current one */
                if (CurrentUsage == Usage)
                {
                    /* it does */
                    bFound = TRUE;
                    break;
                }

                /* move to next usage */
                SubIndex++;
            }while(SubIndex < UsageListLength);

            /* was the usage found ?*/
            if (!bFound)
            {
                /* store it in the break usage list */
                BreakUsageList[BreakUsageIndex] = CurrentUsage;
                BreakUsageIndex++;
            }

            /* move to next usage */
            Index++;

        }while(Index < UsageListLength);

        /* now process the new items */
        Index = 0;
        do
        {
            /* get current usage */
            CurrentUsage = CurrentUsageList[Index];

            /* is the end of list reached? */
            if (!CurrentUsage)
                break;

            /* start searching in current usage list */
            SubIndex = 0;
            bFound = FALSE;
            do
            {
                /* get usage of previous list */
                Usage = PreviousUsageList[SubIndex];

                /* end of list reached? */
                if (!Usage)
                    break;

                /* check if it matches the current one */
                if (CurrentUsage == Usage)
                {
                    /* it does */
                    bFound = TRUE;
                    break;
                }

                /* move to next usage */
                SubIndex++;
            }while(SubIndex < UsageListLength);

            /* was the usage found ?*/
            if (!bFound)
            {
                /* store it in the make usage list */
                MakeUsageList[MakeUsageIndex] = CurrentUsage;
                MakeUsageIndex++;
            }

            /* move to next usage */
            Index++;

        }while(Index < UsageListLength);
    }

    /* does the break list contain empty entries */
    if (BreakUsageIndex < UsageListLength)
    {
        /* zeroize entries */
        RtlZeroMemory(&BreakUsageList[BreakUsageIndex], sizeof(USAGE) * (UsageListLength - BreakUsageIndex));
    }

    /* does the make usage list contain empty entries */
    if (MakeUsageIndex < UsageListLength)
    {
        /* zeroize entries */
        RtlZeroMemory(&MakeUsageList[MakeUsageIndex], sizeof(USAGE) * (UsageListLength - MakeUsageIndex));
    }

    /* done */
    return HIDP_STATUS_SUCCESS;
}

HIDAPI
NTSTATUS
NTAPI
HidParser_TranslateUsageAndPagesToI8042ScanCodes(
   IN PUSAGE_AND_PAGE  ChangedUsageList,
   IN ULONG  UsageListLength,
   IN HIDP_KEYBOARD_DIRECTION  KeyAction,
   IN OUT PHIDP_KEYBOARD_MODIFIER_STATE  ModifierState,
   IN PHIDP_INSERT_SCANCODES  InsertCodesProcedure,
   IN PVOID  InsertCodesContext)
{
    ULONG Index;
    NTSTATUS Status = HIDP_STATUS_SUCCESS;

    for(Index = 0; Index < UsageListLength; Index++)
    {
        if (ChangedUsageList[Index].Usage == 0)
            break;

        //
        // check current usage
        //
        if (ChangedUsageList[Index].UsagePage == HID_USAGE_PAGE_KEYBOARD)
        {
            //
            // process keyboard usage
            //
            Status = HidParser_TranslateKbdUsage(ChangedUsageList[Index].Usage, KeyAction, ModifierState, InsertCodesProcedure, InsertCodesContext);
        }
        else if (ChangedUsageList[Index].UsagePage == HID_USAGE_PAGE_CONSUMER)
        {
            //
            // process consumer usage
            //
            Status = HidParser_TranslateCustUsage(ChangedUsageList[Index].Usage, KeyAction, ModifierState, InsertCodesProcedure, InsertCodesContext);
        }
        else
        {
            //
            // invalid page
            //
            return HIDP_STATUS_I8042_TRANS_UNKNOWN;
        }

        //
        // check status
        //
        if (Status != HIDP_STATUS_SUCCESS)
        {
            //
            // failed
            //
            return Status;
        }
    }

    //
    // return status
    //
    return Status;
}

HIDAPI
NTSTATUS
NTAPI
HidParser_UsageAndPageListDifference(
   IN PUSAGE_AND_PAGE  PreviousUsageList,
   IN PUSAGE_AND_PAGE  CurrentUsageList,
   OUT PUSAGE_AND_PAGE  BreakUsageList,
   OUT PUSAGE_AND_PAGE  MakeUsageList,
   IN ULONG  UsageListLength)
{
    ULONG Index, SubIndex, BreakUsageListIndex = 0, MakeUsageListIndex = 0, bFound;
    PUSAGE_AND_PAGE CurrentUsage, Usage;

    if (UsageListLength)
    {
        /* process removed usages */
        Index = 0;
        do
        {
            /* get usage from current index */
            CurrentUsage = &PreviousUsageList[Index];

            /* end of list reached? */
            if (CurrentUsage->Usage == 0 && CurrentUsage->UsagePage == 0)
                break;

            /* search in current list */
            SubIndex = 0;
            bFound = FALSE;
            do
            {
                /* get usage */
                Usage = &CurrentUsageList[SubIndex];

                /* end of list reached? */
                if (Usage->Usage == 0 && Usage->UsagePage == 0)
                    break;

                /* does it match */
                if (Usage->Usage == CurrentUsage->Usage && Usage->UsagePage == CurrentUsage->UsagePage)
                {
                    /* found match */
                    bFound = TRUE;
                }

                /* move to next index */
                SubIndex++;

            }while(SubIndex < UsageListLength);

            if (!bFound)
            {
                /* store it in break usage list */
                BreakUsageList[BreakUsageListIndex].Usage = CurrentUsage->Usage;
                BreakUsageList[BreakUsageListIndex].UsagePage = CurrentUsage->UsagePage;
                BreakUsageListIndex++;
            }

            /* move to next index */
            Index++;

        }while(Index < UsageListLength);

        /* process new usages */
        Index = 0;
        do
        {
            /* get usage from current index */
            CurrentUsage = &CurrentUsageList[Index];

            /* end of list reached? */
            if (CurrentUsage->Usage == 0 && CurrentUsage->UsagePage == 0)
                break;

            /* search in current list */
            SubIndex = 0;
            bFound = FALSE;
            do
            {
                /* get usage */
                Usage = &PreviousUsageList[SubIndex];

                /* end of list reached? */
                if (Usage->Usage == 0 && Usage->UsagePage == 0)
                    break;

                /* does it match */
                if (Usage->Usage == CurrentUsage->Usage && Usage->UsagePage == CurrentUsage->UsagePage)
                {
                    /* found match */
                    bFound = TRUE;
                }

                /* move to next index */
                SubIndex++;

            }while(SubIndex < UsageListLength);

            if (!bFound)
            {
                /* store it in break usage list */
                MakeUsageList[MakeUsageListIndex].Usage = CurrentUsage->Usage;
                MakeUsageList[MakeUsageListIndex].UsagePage = CurrentUsage->UsagePage;
                MakeUsageListIndex++;
            }

            /* move to next index */
            Index++;
        }while(Index < UsageListLength);
    }

    /* are there remaining free list entries */
    if (BreakUsageListIndex < UsageListLength)
    {
        /* zero them */
        RtlZeroMemory(&BreakUsageList[BreakUsageListIndex], (UsageListLength - BreakUsageListIndex) * sizeof(USAGE_AND_PAGE));
    }

    /* are there remaining free list entries */
    if (MakeUsageListIndex < UsageListLength)
    {
        /* zero them */
        RtlZeroMemory(&MakeUsageList[MakeUsageListIndex], (UsageListLength - MakeUsageListIndex) * sizeof(USAGE_AND_PAGE));
    }

    /* done */
    return HIDP_STATUS_SUCCESS;
}
