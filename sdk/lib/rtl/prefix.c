/*
 * PROJECT:         ReactOS Run-Time Library
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * PURPOSE:         Rtl Prefix Routines
 */

/* INCLUDES *******************************************************************/

#include <rtl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

static
BOOLEAN
PfxpIsPrefixBoundary(
    _In_ PSTRING FullName,
    _In_ USHORT Length)
{
    if (Length > FullName->Length)
        return FALSE;

    if (Length == FullName->Length)
        return TRUE;

    if (FullName->Buffer[Length] == '\\')
        return TRUE;

    return (Length != 0 && FullName->Buffer[Length - 1] == '\\');
}

static
VOID
PfxpReplaceTreeRoot(
    _Inout_ PPREFIX_TABLE_ENTRY *Link,
    _In_ PPREFIX_TABLE_ENTRY OldRoot,
    _In_opt_ PPREFIX_TABLE_ENTRY NewRoot)
{
    PPREFIX_TABLE_ENTRY Next = OldRoot->NextPrefixTree;

    OldRoot->NextPrefixTree = NULL;
    if (NewRoot == NULL)
    {
        *Link = Next;
        return;
    }

    NewRoot->NextPrefixTree = Next;
    *Link = NewRoot;
}

VOID
NTAPI
PfxInitialize(
    _Out_ PPREFIX_TABLE PrefixTable)
{
    PAGED_CODE_RTL();

    PrefixTable->NodeTypeCode = 0;
    PrefixTable->NameLength = 0;
    PrefixTable->NextPrefixTree = NULL;
}

BOOLEAN
NTAPI
PfxInsertPrefix(
    _In_ PPREFIX_TABLE PrefixTable,
    _In_ PSTRING Prefix,
    _Out_ PPREFIX_TABLE_ENTRY PrefixTableEntry)
{
    PPREFIX_TABLE_ENTRY *Link;
    PPREFIX_TABLE_ENTRY Tree, Current;
    PRTL_SPLAY_LINKS Node;
    LONG Result;

    PAGED_CODE_RTL();

    if (Prefix->Length > MAXSHORT)
        return FALSE;

    PrefixTableEntry->NodeTypeCode = 0;
    PrefixTableEntry->NameLength = (CSHORT)Prefix->Length;
    PrefixTableEntry->NextPrefixTree = NULL;
    PrefixTableEntry->Prefix = Prefix;
    RtlInitializeSplayLinks(&PrefixTableEntry->Links);

    Link = &PrefixTable->NextPrefixTree;
    while (*Link != NULL && (*Link)->NameLength > PrefixTableEntry->NameLength)
        Link = &(*Link)->NextPrefixTree;

    Tree = *Link;
    if (Tree == NULL || Tree->NameLength != PrefixTableEntry->NameLength)
    {
        PrefixTableEntry->NextPrefixTree = Tree;
        *Link = PrefixTableEntry;
        return TRUE;
    }

    Node = &Tree->Links;
    for (;;)
    {
        Current = CONTAINING_RECORD(Node, PREFIX_TABLE_ENTRY, Links);
        Result = RtlCompareString(Prefix, Current->Prefix, FALSE);
        if (Result == 0)
            return FALSE;

        if (Result < 0)
        {
            if (RtlLeftChild(Node) == NULL)
            {
                RtlInsertAsLeftChild(Node, &PrefixTableEntry->Links);
                break;
            }
            Node = RtlLeftChild(Node);
        }
        else
        {
            if (RtlRightChild(Node) == NULL)
            {
                RtlInsertAsRightChild(Node, &PrefixTableEntry->Links);
                break;
            }
            Node = RtlRightChild(Node);
        }
    }

    RtlSplay(&PrefixTableEntry->Links);
    PfxpReplaceTreeRoot(Link, Tree, PrefixTableEntry);
    return TRUE;
}

VOID
NTAPI
PfxRemovePrefix(
    _In_ PPREFIX_TABLE PrefixTable,
    _In_ PPREFIX_TABLE_ENTRY PrefixTableEntry)
{
    PPREFIX_TABLE_ENTRY *Link;
    PPREFIX_TABLE_ENTRY Tree;
    PRTL_SPLAY_LINKS Root;

    PAGED_CODE_RTL();

    Root = &PrefixTableEntry->Links;
    while (!RtlIsRoot(Root))
        Root = RtlParent(Root);
    Tree = CONTAINING_RECORD(Root, PREFIX_TABLE_ENTRY, Links);

    Link = &PrefixTable->NextPrefixTree;
    while (*Link != NULL && *Link != Tree)
        Link = &(*Link)->NextPrefixTree;

    if (*Link == NULL)
        return;

    Root = RtlDelete(&PrefixTableEntry->Links);
    PfxpReplaceTreeRoot(Link,
                        Tree,
                        Root ? CONTAINING_RECORD(Root, PREFIX_TABLE_ENTRY, Links) : NULL);
    RtlInitializeSplayLinks(&PrefixTableEntry->Links);
}

PPREFIX_TABLE_ENTRY
NTAPI
PfxFindPrefix(
    _In_ PPREFIX_TABLE PrefixTable,
    _In_ PSTRING FullName)
{
    PPREFIX_TABLE_ENTRY *Link;
    PPREFIX_TABLE_ENTRY Tree, Current;
    PRTL_SPLAY_LINKS Node;
    STRING Candidate;
    LONG Result;

    PAGED_CODE_RTL();

    for (Link = &PrefixTable->NextPrefixTree; (Tree = *Link) != NULL; Link = &Tree->NextPrefixTree)
    {
        if (!PfxpIsPrefixBoundary(FullName, (USHORT)Tree->NameLength))
            continue;

        Candidate.Length = (USHORT)Tree->NameLength;
        Candidate.MaximumLength = Candidate.Length;
        Candidate.Buffer = FullName->Buffer;

        Node = &Tree->Links;
        while (Node != NULL)
        {
            Current = CONTAINING_RECORD(Node, PREFIX_TABLE_ENTRY, Links);
            Result = RtlCompareString(&Candidate, Current->Prefix, FALSE);
            if (Result == 0)
            {
                if (Current != Tree)
                {
                    RtlSplay(Node);
                    PfxpReplaceTreeRoot(Link, Tree, Current);
                }
                return Current;
            }

            Node = (Result < 0) ? RtlLeftChild(Node) : RtlRightChild(Node);
        }
    }

    return NULL;
}
