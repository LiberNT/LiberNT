/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Taking an OEM path apart and saying what a FAT or HPFS name may hold
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_DBCS_TAG 'dRsF'

#define FSRTL_STACK_STATES 64
#define FSRTL_STATE_ACTIVE 1
#define FSRTL_STATE_BOUNDED 2

#define FSRTL_FAT_NAME_BYTES 8
#define FSRTL_FAT_EXTENSION_BYTES 3
#define FSRTL_HPFS_NAME_BYTES 255

typedef struct _FSRTL_DBCS_MATCH_RUN
{
    const UCHAR *Expression;
    ULONG ExpressionLength;
    const UCHAR *Name;
    ULONG NameLength;
    LONG LastDot;
    PUCHAR Current;
    PUCHAR Next;
} FSRTL_DBCS_MATCH_RUN, *PFSRTL_DBCS_MATCH_RUN;

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
FsRtlpDbcsCharSize(
    _In_reads_bytes_(Remaining) const UCHAR *Bytes,
    _In_ ULONG Remaining)
{
    if ((Remaining >= 2) && FsRtlIsLeadDbcsCharacter(Bytes[0])) return 2;

    return 1;
}

static
BOOLEAN
FsRtlpIsAnsiWildCard(
    _In_ UCHAR Character)
{
    return (Character == '*') || (Character == '?') ||
           (Character == ANSI_DOS_STAR) || (Character == ANSI_DOS_QM) ||
           (Character == ANSI_DOS_DOT);
}

static
LONG
FsRtlpLastDbcsDot(
    _In_reads_bytes_(Length) const UCHAR *Name,
    _In_ ULONG Length)
{
    LONG LastDot = -1;
    ULONG Index = 0;

    while (Index < Length)
    {
        ULONG Size = FsRtlpDbcsCharSize(&Name[Index], Length - Index);

        if ((Size == 1) && (Name[Index] == '.')) LastDot = (LONG)Index;

        Index += Size;
    }

    return LastDot;
}

static
VOID
FsRtlpCloseDbcsStates(
    _In_ PFSRTL_DBCS_MATCH_RUN Run,
    _In_ ULONG NamePosition)
{
    ULONG Length = Run->ExpressionLength;
    ULONG i = 0;

    while (i < Length)
    {
        ULONG Size = FsRtlpDbcsCharSize(&Run->Expression[i], Length - i);
        UCHAR Character = Run->Expression[i];

        if (!Run->Current[i] || (Size != 1))
        {
            i += Size;
            continue;
        }

        if (Character == '*')
        {
            Run->Current[i + 1] |= FSRTL_STATE_ACTIVE;
        }
        else if (Character == ANSI_DOS_STAR)
        {
            /* DOS_STAR may always stop here, matching nothing more */
            Run->Current[i + 1] |= FSRTL_STATE_ACTIVE;
        }
        else if (Character == ANSI_DOS_QM)
        {
            if ((NamePosition == Run->NameLength) ||
                (Run->Name[NamePosition] == '.'))
            {
                ULONG j = i;

                while ((j < Length) && (Run->Expression[j] == ANSI_DOS_QM)) j++;

                Run->Current[j] |= FSRTL_STATE_ACTIVE;
            }
        }
        else if ((Character == ANSI_DOS_DOT) &&
                 (NamePosition == Run->NameLength))
        {
            Run->Current[i + 1] |= FSRTL_STATE_ACTIVE;
        }

        i += Size;
    }
}

static
BOOLEAN
FsRtlpSweepDbcsExpression(
    _In_ PFSRTL_DBCS_MATCH_RUN Run)
{
    ULONG States = Run->ExpressionLength + 1;
    ULONG Position = 0;

    RtlZeroMemory(Run->Current, States);
    Run->Current[0] = FSRTL_STATE_ACTIVE;

    for (;;)
    {
        ULONG NameSize;
        ULONG i;
        PUCHAR Swap;

        FsRtlpCloseDbcsStates(Run, Position);

        if (Position == Run->NameLength) break;

        NameSize = FsRtlpDbcsCharSize(&Run->Name[Position],
                                      Run->NameLength - Position);
        RtlZeroMemory(Run->Next, States);

        i = 0;

        while (i < Run->ExpressionLength)
        {
            ULONG Size = FsRtlpDbcsCharSize(&Run->Expression[i],
                                            Run->ExpressionLength - i);
            UCHAR Character = Run->Expression[i];

            if (!Run->Current[i])
            {
                i += Size;
                continue;
            }

            if ((Size == 1) && (Character == '*'))
            {
                Run->Next[i] |= FSRTL_STATE_ACTIVE;
            }
            else if ((Size == 1) && (Character == '?'))
            {
                Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
            }
            else if ((Size == 1) && (Character == ANSI_DOS_QM))
            {
                if (Run->Name[Position] != '.')
                {
                    Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
                }
                else
                {
                    ULONG j = i;

                    /* A run of DOS_QM that ends the expression takes the period with it */
                    while ((j < Run->ExpressionLength) && (Run->Expression[j] == ANSI_DOS_QM)) j++;
                    if (j == Run->ExpressionLength) Run->Next[j] |= FSRTL_STATE_ACTIVE;
                }
            }
            else if ((Size == 1) && (Character == ANSI_DOS_DOT))
            {
                if (Run->Name[Position] == '.') Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
            }
            else if ((Size == 1) && (Character == ANSI_DOS_STAR))
            {
                /* It takes characters up to and including the name's final period, and past it only when it started there */
                if (Run->Current[i] & FSRTL_STATE_ACTIVE)
                {
                    Run->Next[i] |= ((LONG)Position == Run->LastDot) ? FSRTL_STATE_BOUNDED : FSRTL_STATE_ACTIVE;
                }
            }
            else if ((Size == NameSize) &&
                     (RtlCompareMemory(&Run->Expression[i],
                                       &Run->Name[Position],
                                       Size) == Size))
            {
                Run->Next[i + Size] |= FSRTL_STATE_ACTIVE;
            }

            i += Size;
        }

        Position += NameSize;

        Swap = Run->Current;
        Run->Current = Run->Next;
        Run->Next = Swap;
    }

    return (BOOLEAN)(Run->Current[Run->ExpressionLength] != FALSE);
}

static
BOOLEAN
FsRtlpIsDbcsDotName(
    _In_reads_bytes_(Length) const UCHAR *Component,
    _In_ ULONG Length)
{
    if ((Length == 1) && (Component[0] == '.')) return TRUE;

    return (BOOLEAN)((Length == 2) && (Component[0] == '.') &&
                     (Component[1] == '.'));
}

/*
 * A name that holds a wildcard is a pattern, not a file name, and a pattern is
 * held only to the characters it may use: the 8.3 shape is what the names it
 * is matched against have to keep.
 */
static
BOOLEAN
FsRtlpValidateFatComponent(
    _In_reads_bytes_(Length) const UCHAR *Component,
    _In_ ULONG Length,
    _In_ BOOLEAN WildCardsPermissible)
{
    ULONG NameBytes = 0;
    ULONG ExtensionBytes = 0;
    ULONG Dots = 0;
    BOOLEAN Shaped = TRUE;
    BOOLEAN Wild = FALSE;
    ULONG Index = 0;

    if (Length == 0) return FALSE;

    if (FsRtlpIsDbcsDotName(Component, Length)) return WildCardsPermissible;

    while (Index < Length)
    {
        ULONG Size = FsRtlpDbcsCharSize(&Component[Index], Length - Index);

        if ((Size == 1) && FsRtlIsLeadDbcsCharacter(Component[Index]))
        {
            return FALSE;
        }

        if ((Size == 1) && (Component[Index] == '.'))
        {
            if ((NameBytes == 0) || (Component[Index - 1] == ' '))
            {
                Shaped = FALSE;
            }

            Dots++;
            Index++;
            continue;
        }

        if ((Size == 1) && FsRtlIsAnsiCharacterWild(Component[Index]))
        {
            if (!WildCardsPermissible) return FALSE;

            Wild = TRUE;
        }
        else if ((Size == 1) &&
                 !FsRtlIsAnsiCharacterLegalFat(Component[Index], FALSE))
        {
            return FALSE;
        }

        if (Dots != 0) ExtensionBytes += Size; else NameBytes += Size;

        Index += Size;
    }

    if (Wild) return TRUE;

    if (!Shaped || (Dots > 1)) return FALSE;
    if (Component[Length - 1] == ' ') return FALSE;
    if ((NameBytes == 0) || (NameBytes > FSRTL_FAT_NAME_BYTES)) return FALSE;
    if (ExtensionBytes > FSRTL_FAT_EXTENSION_BYTES) return FALSE;
    if ((Dots != 0) && (ExtensionBytes == 0)) return FALSE;

    return TRUE;
}

static
BOOLEAN
FsRtlpValidateHpfsComponent(
    _In_reads_bytes_(Length) const UCHAR *Component,
    _In_ ULONG Length,
    _In_ BOOLEAN WildCardsPermissible)
{
    ULONG Index = 0;

    if (Length == 0) return FALSE;
    if (Length > FSRTL_HPFS_NAME_BYTES) return FALSE;

    if (FsRtlpIsDbcsDotName(Component, Length)) return WildCardsPermissible;

    while (Index < Length)
    {
        ULONG Size = FsRtlpDbcsCharSize(&Component[Index], Length - Index);

        if ((Size == 1) && FsRtlIsLeadDbcsCharacter(Component[Index]))
        {
            return FALSE;
        }

        if ((Size == 1) &&
            !FsRtlIsAnsiCharacterLegalHpfs(Component[Index],
                                           WildCardsPermissible))
        {
            return FALSE;
        }

        Index += Size;
    }

    if (Component[Length - 1] == ' ') return FALSE;
    if (Component[Length - 1] == '.') return FALSE;

    return TRUE;
}

/*
 * A path is names with one separator between each two. The separator in
 * front, and the one a path may end with, are not names: a lone backslash is
 * the root and needs only the leading separator to be allowed.
 */
static
BOOLEAN
FsRtlpIsDbcsPathLegal(
    _In_ ANSI_STRING DbcsName,
    _In_ BOOLEAN WildCardsPermissible,
    _In_ BOOLEAN PathNamePermissible,
    _In_ BOOLEAN LeadingBackslashPermissible,
    _In_ BOOLEAN Fat)
{
    const UCHAR *Buffer = (const UCHAR *)DbcsName.Buffer;
    ULONG Length = DbcsName.Length;
    ULONG Index = 0;

    if ((Length == 0) || (Buffer == NULL)) return FALSE;

    if (Buffer[0] == '\\')
    {
        if (!LeadingBackslashPermissible) return FALSE;

        Index = 1;
        if (Index == Length) return TRUE;
    }

    for (;;)
    {
        ULONG Start = Index;
        ULONG ComponentLength;

        while (Index < Length)
        {
            ULONG Size = FsRtlpDbcsCharSize(&Buffer[Index], Length - Index);

            if ((Size == 1) && (Buffer[Index] == '\\')) break;

            Index += Size;
        }

        ComponentLength = Index - Start;

        if (Fat)
        {
            if (!FsRtlpValidateFatComponent(&Buffer[Start],
                                            ComponentLength,
                                            WildCardsPermissible))
            {
                return FALSE;
            }
        }
        else if (!FsRtlpValidateHpfsComponent(&Buffer[Start],
                                              ComponentLength,
                                              WildCardsPermissible))
        {
            return FALSE;
        }

        if (Index == Length) return TRUE;

        if (!PathNamePermissible) return FALSE;

        Index++;
        if (Index == Length) return TRUE;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlDissectDbcs(
    _In_ ANSI_STRING Name,
    _Out_ PANSI_STRING FirstPart,
    _Out_ PANSI_STRING RemainingPart)
{
    const UCHAR *Buffer = (const UCHAR *)Name.Buffer;
    ULONG Start;
    ULONG Index;

    PAGED_CODE();

    FirstPart->Length = 0;
    FirstPart->MaximumLength = 0;
    FirstPart->Buffer = NULL;
    RemainingPart->Length = 0;
    RemainingPart->MaximumLength = 0;
    RemainingPart->Buffer = NULL;

    if ((Name.Length == 0) || (Buffer == NULL)) return;

    /* A leading separator belongs to neither part */
    Start = (Buffer[0] == '\\') ? 1 : 0;

    Index = Start;
    while (Index < Name.Length)
    {
        ULONG Size = FsRtlpDbcsCharSize(&Buffer[Index], Name.Length - Index);

        if ((Size == 1) && (Buffer[Index] == '\\')) break;

        Index += Size;
    }

    ASSERT(Index <= Name.Length);

    FirstPart->Buffer = Name.Buffer + Start;
    FirstPart->Length = (USHORT)(Index - Start);
    FirstPart->MaximumLength = FirstPart->Length;

    if (Index == Name.Length) return;

    RemainingPart->Buffer = Name.Buffer + Index + 1;
    RemainingPart->Length = (USHORT)(Name.Length - Index - 1);
    RemainingPart->MaximumLength = RemainingPart->Length;
}

BOOLEAN
NTAPI
FsRtlDoesDbcsContainWildCards(
    _In_ PANSI_STRING Name)
{
    const UCHAR *Buffer = (const UCHAR *)Name->Buffer;
    ULONG Index = 0;

    PAGED_CODE();

    while (Index < Name->Length)
    {
        ULONG Size = FsRtlpDbcsCharSize(&Buffer[Index], Name->Length - Index);

        if ((Size == 1) && FsRtlpIsAnsiWildCard(Buffer[Index])) return TRUE;

        Index += Size;
    }

    return FALSE;
}

BOOLEAN
NTAPI
FsRtlIsDbcsInExpression(
    _In_ PANSI_STRING Expression,
    _In_ PANSI_STRING Name)
{
    UCHAR StackStates[2 * FSRTL_STACK_STATES];
    FSRTL_DBCS_MATCH_RUN Run;
    PUCHAR Buffer;
    ULONG States;
    BOOLEAN Matched;

    PAGED_CODE();

    Run.Expression = (const UCHAR *)Expression->Buffer;
    Run.ExpressionLength = Expression->Length;
    Run.Name = (const UCHAR *)Name->Buffer;
    Run.NameLength = Name->Length;

    if (Run.NameLength == 0) return (BOOLEAN)(Run.ExpressionLength == 0);
    if (Run.ExpressionLength == 0) return FALSE;

    if ((Run.ExpressionLength == 1) && (Run.Expression[0] == '*'))
    {
        return TRUE;
    }

    if ((Run.ExpressionLength == 3) && (Run.Expression[0] == '*') &&
        (Run.Expression[1] == '.') && (Run.Expression[2] == '*'))
    {
        return TRUE;
    }

    Run.LastDot = FsRtlpLastDbcsDot(Run.Name, Run.NameLength);

    States = Run.ExpressionLength + 1;

    if (States <= FSRTL_STACK_STATES)
    {
        Buffer = StackStates;
    }
    else
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, 2 * States, FSRTL_DBCS_TAG);
        if (Buffer == NULL) ExRaiseStatus(STATUS_NO_MEMORY);
    }

    Run.Current = Buffer;
    Run.Next = Buffer + States;

    Matched = FsRtlpSweepDbcsExpression(&Run);

    if (Buffer != StackStates) ExFreePoolWithTag(Buffer, FSRTL_DBCS_TAG);

    return Matched;
}

BOOLEAN
NTAPI
FsRtlIsFatDbcsLegal(
    _In_ ANSI_STRING DbcsName,
    _In_ BOOLEAN WildCardsPermissible,
    _In_ BOOLEAN PathNamePermissible,
    _In_ BOOLEAN LeadingBackslashPermissible)
{
    PAGED_CODE();

    return FsRtlpIsDbcsPathLegal(DbcsName,
                                 WildCardsPermissible,
                                 PathNamePermissible,
                                 LeadingBackslashPermissible,
                                 TRUE);
}

BOOLEAN
NTAPI
FsRtlIsHpfsDbcsLegal(
    _In_ ANSI_STRING DbcsName,
    _In_ BOOLEAN WildCardsPermissible,
    _In_ BOOLEAN PathNamePermissible,
    _In_ BOOLEAN LeadingBackslashPermissible)
{
    PAGED_CODE();

    return FsRtlpIsDbcsPathLegal(DbcsName,
                                 WildCardsPermissible,
                                 PathNamePermissible,
                                 LeadingBackslashPermissible,
                                 FALSE);
}

/* EOF */
