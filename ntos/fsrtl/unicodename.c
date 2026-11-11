/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Taking a Unicode path apart and matching it against an expression
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_NAME_TAG 'nRsF'

#define FSRTL_STACK_STATES 64

#define FSRTL_STATE_ACTIVE 1
#define FSRTL_STATE_BOUNDED 2

/*
 * One run of the matcher: the expression, the name and the two state sets the
 * sweep needs.
 */
typedef struct _FSRTL_MATCH_RUN
{
    const WCHAR *Expression;
    ULONG ExpressionLength;
    const WCHAR *Name;
    ULONG NameLength;
    LONG LastDot;
    BOOLEAN IgnoreCase;
    const WCHAR *UpcaseTable;
    PUCHAR Current;
    PUCHAR Next;
} FSRTL_MATCH_RUN, *PFSRTL_MATCH_RUN;

/* PRIVATE FUNCTIONS **********************************************************/

static
WCHAR
FsRtlpUpcaseChar(
    _In_ PFSRTL_MATCH_RUN Run,
    _In_ WCHAR Character)
{
    if (!Run->IgnoreCase) return Character;

    if (Run->UpcaseTable != NULL) return Run->UpcaseTable[Character];

    return RtlUpcaseUnicodeChar(Character);
}

static
BOOLEAN
FsRtlpIsWildCard(
    _In_ WCHAR Character)
{
    return (Character == L'*') || (Character == L'?') ||
           (Character == DOS_STAR) || (Character == DOS_QM) ||
           (Character == DOS_DOT);
}

static
VOID
FsRtlpCloseStates(
    _In_ PFSRTL_MATCH_RUN Run,
    _In_ ULONG NamePosition)
{
    ULONG Length = Run->ExpressionLength;
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        WCHAR Character;

        if (!Run->Current[i]) continue;

        Character = Run->Expression[i];

        if (Character == L'*')
        {
            Run->Current[i + 1] |= FSRTL_STATE_ACTIVE;
            continue;
        }

        /* DOS_STAR may always stop here, matching nothing more */
        if (Character == DOS_STAR)
        {
            Run->Current[i + 1] |= FSRTL_STATE_ACTIVE;
            continue;
        }

        if (Character == DOS_QM)
        {
            if ((NamePosition == Run->NameLength) ||
                (Run->Name[NamePosition] == L'.'))
            {
                ULONG j = i;

                while ((j < Length) && (Run->Expression[j] == DOS_QM)) j++;

                Run->Current[j] |= FSRTL_STATE_ACTIVE;
            }

            continue;
        }

        if ((Character == DOS_DOT) && (NamePosition == Run->NameLength))
        {
            Run->Current[i + 1] |= FSRTL_STATE_ACTIVE;
        }
    }
}

static
BOOLEAN
FsRtlpSweepExpression(
    _In_ PFSRTL_MATCH_RUN Run)
{
    ULONG States = Run->ExpressionLength + 1;
    ULONG Position;

    RtlZeroMemory(Run->Current, States);
    Run->Current[0] = FSRTL_STATE_ACTIVE;

    for (Position = 0; ; Position++)
    {
        ULONG i;
        WCHAR Wanted;
        PUCHAR Swap;

        FsRtlpCloseStates(Run, Position);

        if (Position == Run->NameLength) break;

        Wanted = FsRtlpUpcaseChar(Run, Run->Name[Position]);
        RtlZeroMemory(Run->Next, States);

        for (i = 0; i < Run->ExpressionLength; i++)
        {
            WCHAR Character;

            if (!Run->Current[i]) continue;

            Character = Run->Expression[i];

            if (Character == L'*')
            {
                Run->Next[i] |= FSRTL_STATE_ACTIVE;
            }
            else if (Character == L'?')
            {
                Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
            }
            else if (Character == DOS_QM)
            {
                if (Run->Name[Position] != L'.')
                {
                    Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
                }
                else
                {
                    ULONG j = i;

                    /* A run of DOS_QM that ends the expression takes the period with it */
                    while ((j < Run->ExpressionLength) && (Run->Expression[j] == DOS_QM)) j++;
                    if (j == Run->ExpressionLength) Run->Next[j] |= FSRTL_STATE_ACTIVE;
                }
            }
            else if (Character == DOS_DOT)
            {
                if (Run->Name[Position] == L'.') Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
            }
            else if (Character == DOS_STAR)
            {
                /* It takes characters up to and including the name's final period, and past it only when it started there */
                if (Run->Current[i] & FSRTL_STATE_ACTIVE)
                {
                    Run->Next[i] |= ((LONG)Position == Run->LastDot) ? FSRTL_STATE_BOUNDED : FSRTL_STATE_ACTIVE;
                }
            }
            else if (FsRtlpUpcaseChar(Run, Character) == Wanted)
            {
                Run->Next[i + 1] |= FSRTL_STATE_ACTIVE;
            }
        }

        Swap = Run->Current;
        Run->Current = Run->Next;
        Run->Next = Swap;
    }

    return (BOOLEAN)(Run->Current[Run->ExpressionLength] != FALSE);
}

static
BOOLEAN
FsRtlpTailMatches(
    _In_ PFSRTL_MATCH_RUN Run)
{
    ULONG Length = Run->ExpressionLength - 1;
    ULONG i;

    if (Length > Run->NameLength) return FALSE;

    for (i = 0; i < Length; i++)
    {
        WCHAR Left = FsRtlpUpcaseChar(Run, Run->Expression[i + 1]);
        WCHAR Right = FsRtlpUpcaseChar(Run,
                                       Run->Name[Run->NameLength - Length + i]);

        if (Left != Right) return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
FsRtlpHasWildCardAfterFirst(
    _In_ PFSRTL_MATCH_RUN Run)
{
    ULONG i;

    for (i = 1; i < Run->ExpressionLength; i++)
    {
        if (FsRtlpIsWildCard(Run->Expression[i])) return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
FsRtlpMatchName(
    _In_ PCUNICODE_STRING Expression,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN IgnoreCase,
    _In_opt_ const WCHAR *UpcaseTable)
{
    UCHAR StackStates[2 * FSRTL_STACK_STATES];
    FSRTL_MATCH_RUN Run;
    PUCHAR Buffer;
    ULONG States;
    ULONG i;
    BOOLEAN Matched;

    Run.Expression = Expression->Buffer;
    Run.ExpressionLength = Expression->Length / sizeof(WCHAR);
    Run.Name = Name->Buffer;
    Run.NameLength = Name->Length / sizeof(WCHAR);
    Run.IgnoreCase = IgnoreCase;
    Run.UpcaseTable = UpcaseTable;
    Run.LastDot = -1;

    if (Run.NameLength == 0) return (BOOLEAN)(Run.ExpressionLength == 0);
    if (Run.ExpressionLength == 0) return FALSE;

    if ((Run.ExpressionLength == 1) && (Run.Expression[0] == L'*'))
    {
        return TRUE;
    }

    if ((Run.Expression[0] == L'*') && !FsRtlpHasWildCardAfterFirst(&Run))
    {
        if (FsRtlpTailMatches(&Run)) return TRUE;
    }

    for (i = Run.NameLength; i > 0; i--)
    {
        if (Run.Name[i - 1] == L'.')
        {
            Run.LastDot = (LONG)(i - 1);
            break;
        }
    }

    States = Run.ExpressionLength + 1;

    if (States <= FSRTL_STACK_STATES)
    {
        Buffer = StackStates;
    }
    else
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, 2 * States, FSRTL_NAME_TAG);
        if (Buffer == NULL) ExRaiseStatus(STATUS_NO_MEMORY);
    }

    Run.Current = Buffer;
    Run.Next = Buffer + States;

    Matched = FsRtlpSweepExpression(&Run);

    if (Buffer != StackStates) ExFreePoolWithTag(Buffer, FSRTL_NAME_TAG);

    return Matched;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlDissectName(
    _In_ UNICODE_STRING Name,
    _Out_ PUNICODE_STRING FirstPart,
    _Out_ PUNICODE_STRING RemainingPart)
{
    USHORT Count;
    USHORT Start;
    USHORT Offset;

    PAGED_CODE();

    FirstPart->Length = 0;
    FirstPart->MaximumLength = 0;
    FirstPart->Buffer = NULL;
    RemainingPart->Length = 0;
    RemainingPart->MaximumLength = 0;
    RemainingPart->Buffer = NULL;

    if ((Name.Length == 0) || (Name.Buffer == NULL)) return;

    Count = Name.Length / sizeof(WCHAR);

    /* A leading separator belongs to neither part */
    Start = (Name.Buffer[0] == L'\\') ? 1 : 0;

    for (Offset = Start; Offset < Count; Offset++)
    {
        if (Name.Buffer[Offset] == L'\\') break;
    }

    FirstPart->Buffer = Name.Buffer + Start;
    FirstPart->Length = (USHORT)((Offset - Start) * sizeof(WCHAR));
    FirstPart->MaximumLength = FirstPart->Length;

    if (Offset == Count) return;

    RemainingPart->Buffer = Name.Buffer + Offset + 1;
    RemainingPart->Length = (USHORT)((Count - Offset - 1) * sizeof(WCHAR));
    RemainingPart->MaximumLength = RemainingPart->Length;
}

BOOLEAN
NTAPI
FsRtlDoesNameContainWildCards(
    _In_ PUNICODE_STRING Name)
{
    ULONG Length = Name->Length / sizeof(WCHAR);
    ULONG i;

    PAGED_CODE();

    for (i = 0; i < Length; i++)
    {
        if (FsRtlpIsWildCard(Name->Buffer[i])) return TRUE;
    }

    return FALSE;
}

BOOLEAN
NTAPI
FsRtlAreNamesEqual(
    _In_ PCUNICODE_STRING Name1,
    _In_ PCUNICODE_STRING Name2,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_opt_(0x10000) PCWCH UpcaseTable)
{
    ULONG Length;
    ULONG i;

    PAGED_CODE();

    if (Name1->Length != Name2->Length) return FALSE;

    if (!IgnoreCase)
    {
        return (BOOLEAN)(RtlCompareMemory(Name1->Buffer,
                                          Name2->Buffer,
                                          Name1->Length) == Name1->Length);
    }

    if (UpcaseTable == NULL)
    {
        return (BOOLEAN)(RtlCompareUnicodeString(Name1, Name2, TRUE) == 0);
    }

    Length = Name1->Length / sizeof(WCHAR);

    for (i = 0; i < Length; i++)
    {
        if (UpcaseTable[Name1->Buffer[i]] != UpcaseTable[Name2->Buffer[i]])
        {
            return FALSE;
        }
    }

    return TRUE;
}

BOOLEAN
NTAPI
FsRtlIsNameInExpression(
    _In_ PUNICODE_STRING Expression,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN IgnoreCase,
    _In_opt_ PWCHAR UpcaseTable)
{
    PAGED_CODE();

    return FsRtlpMatchName(Expression, Name, IgnoreCase, UpcaseTable);
}

/* EOF */
