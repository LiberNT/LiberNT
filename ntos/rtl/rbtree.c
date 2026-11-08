/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Red-black tree insertion and removal on RTL_BALANCED_NODE
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

static
PRTL_BALANCED_NODE
RtlpRbParent(
    _In_ PRTL_BALANCED_NODE Node)
{
    return (PRTL_BALANCED_NODE)(Node->ParentValue & ~(ULONG_PTR)RTL_BALANCED_NODE_RESERVED_PARENT_MASK);
}

static
VOID
RtlpRbSetParent(
    _Inout_ PRTL_BALANCED_NODE Node,
    _In_opt_ PRTL_BALANCED_NODE Parent)
{
    Node->ParentValue = (ULONG_PTR)Parent | (Node->ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK);
}

static
BOOLEAN
RtlpRbIsRed(
    _In_opt_ PRTL_BALANCED_NODE Node)
{
    return (Node != NULL) && Node->Red;
}

static
VOID
RtlpRbReplaceChild(
    _Inout_ PRTL_RB_TREE Tree,
    _In_opt_ PRTL_BALANCED_NODE Parent,
    _In_ PRTL_BALANCED_NODE Old,
    _In_opt_ PRTL_BALANCED_NODE New)
{
    if (Parent == NULL)
        Tree->Root = New;
    else
        Parent->Children[Parent->Right == Old] = New;
    if (New != NULL)
        RtlpRbSetParent(New, Parent);
}

static
VOID
RtlpRbRotate(
    _Inout_ PRTL_RB_TREE Tree,
    _Inout_ PRTL_BALANCED_NODE Node,
    _In_ ULONG Down)
{
    PRTL_BALANCED_NODE Pivot = Node->Children[!Down];
    PRTL_BALANCED_NODE Inner = Pivot->Children[Down];

    RtlpRbReplaceChild(Tree, RtlpRbParent(Node), Node, Pivot);
    Node->Children[!Down] = Inner;
    if (Inner != NULL)
        RtlpRbSetParent(Inner, Node);
    Pivot->Children[Down] = Node;
    RtlpRbSetParent(Node, Pivot);
}

VOID
NTAPI
RtlRbInsertNodeEx(
    _Inout_ PRTL_RB_TREE Tree,
    _In_opt_ PRTL_BALANCED_NODE Parent,
    _In_ BOOLEAN Right,
    _Inout_ PRTL_BALANCED_NODE Node)
{
    PRTL_BALANCED_NODE Grand, Uncle;
    ULONG Side;

    Node->Left = NULL;
    Node->Right = NULL;
    Node->ParentValue = (ULONG_PTR)Parent;

    if (Parent == NULL)
    {
        Tree->Root = Node;
        Tree->Min = Node;
        return;
    }

    Parent->Children[Right != FALSE] = Node;
    Node->Red = 1;
    if (!Right && (Tree->Min == Parent))
        Tree->Min = Node;

    while (((Parent = RtlpRbParent(Node)) != NULL) && Parent->Red)
    {
        Grand = RtlpRbParent(Parent);
        Side = (Grand->Right == Parent);
        Uncle = Grand->Children[!Side];
        if (RtlpRbIsRed(Uncle))
        {
            Parent->Red = 0;
            Uncle->Red = 0;
            Grand->Red = 1;
            Node = Grand;
            continue;
        }
        if (Node == Parent->Children[!Side])
        {
            RtlpRbRotate(Tree, Parent, Side);
            Node = Parent;
            Parent = RtlpRbParent(Node);
        }
        Parent->Red = 0;
        Grand->Red = 1;
        RtlpRbRotate(Tree, Grand, !Side);
    }
    Tree->Root->Red = 0;
}

VOID
NTAPI
RtlRbRemoveNode(
    _Inout_ PRTL_RB_TREE Tree,
    _In_ PRTL_BALANCED_NODE Node)
{
    PRTL_BALANCED_NODE Child, Parent, Sibling, Next;
    BOOLEAN RemovedRed;
    ULONG Side;

    if (Tree->Min == Node)
    {
        if (Node->Right != NULL)
        {
            for (Next = Node->Right; Next->Left != NULL; Next = Next->Left)
                ;
            Tree->Min = Next;
        }
        else
        {
            Tree->Min = RtlpRbParent(Node);
        }
    }

    if ((Node->Left == NULL) || (Node->Right == NULL))
    {
        Child = (Node->Left != NULL) ? Node->Left : Node->Right;
        Parent = RtlpRbParent(Node);
        RemovedRed = Node->Red;
        RtlpRbReplaceChild(Tree, Parent, Node, Child);
    }
    else
    {
        for (Next = Node->Right; Next->Left != NULL; Next = Next->Left)
            ;
        RemovedRed = Next->Red;
        Child = Next->Right;
        if (RtlpRbParent(Next) == Node)
        {
            Parent = Next;
        }
        else
        {
            Parent = RtlpRbParent(Next);
            RtlpRbReplaceChild(Tree, Parent, Next, Child);
            Next->Right = Node->Right;
            RtlpRbSetParent(Next->Right, Next);
        }
        RtlpRbReplaceChild(Tree, RtlpRbParent(Node), Node, Next);
        Next->Left = Node->Left;
        RtlpRbSetParent(Next->Left, Next);
        Next->Red = Node->Red;
    }

    if (RemovedRed)
        return;

    while ((Child != Tree->Root) && !RtlpRbIsRed(Child))
    {
        Side = (Parent->Left == Child) ? 0 : 1;
        Sibling = Parent->Children[!Side];
        if (RtlpRbIsRed(Sibling))
        {
            Sibling->Red = 0;
            Parent->Red = 1;
            RtlpRbRotate(Tree, Parent, Side);
            Sibling = Parent->Children[!Side];
        }
        if (!RtlpRbIsRed(Sibling->Left) && !RtlpRbIsRed(Sibling->Right))
        {
            Sibling->Red = 1;
            Child = Parent;
            Parent = RtlpRbParent(Child);
            continue;
        }
        if (!RtlpRbIsRed(Sibling->Children[!Side]))
        {
            Sibling->Children[Side]->Red = 0;
            Sibling->Red = 1;
            RtlpRbRotate(Tree, Sibling, !Side);
            Sibling = Parent->Children[!Side];
        }
        Sibling->Red = Parent->Red;
        Parent->Red = 0;
        Sibling->Children[!Side]->Red = 0;
        RtlpRbRotate(Tree, Parent, Side);
        Child = Tree->Root;
        break;
    }
    if (Child != NULL)
        Child->Red = 0;
}
