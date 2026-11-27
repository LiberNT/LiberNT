/*
 * PROJECT:     LiberNT runtime loader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Module dependency nodes and load references
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntdll.h>

typedef struct _LDRP_NODE
{
    LDR_DDAG_NODE Node;
    struct _LDRP_NODE *VisitNext;
    ULONG Mark;
} LDRP_NODE;

static VOID
LdrpAppendLink(LDRP_CSLIST *List, PSINGLE_LIST_ENTRY Link)
{
    if (List->Tail)
    {
        Link->Next = List->Tail->Next;
        List->Tail->Next = Link;
    }
    else
        Link->Next = Link;
    List->Tail = Link;
}

static VOID
LdrpRemoveLink(LDRP_CSLIST *List, PSINGLE_LIST_ENTRY Link)
{
    PSINGLE_LIST_ENTRY Previous = List->Tail;
    while (Previous->Next != Link)
        Previous = Previous->Next;
    Previous->Next = Link->Next;
    if (List->Tail == Link)
        List->Tail = Previous == Link ? NULL : Previous;
}

/* Windows tags the owner of a dynamic forwarder edge in bit zero. */
static PLDR_DDAG_NODE
LdrpDependencyOwner(PLDR_DEPENDENCY_RECORD Edge)
{
    return (PLDR_DDAG_NODE)((ULONG_PTR)Edge->IncomingDependencyNode & ~(ULONG_PTR)1);
}

static VOID
LdrpUnlinkDependency(PLDR_DEPENDENCY_RECORD Edge)
{
    LdrpRemoveLink(&LdrpDependencyOwner(Edge)->Dependencies, &Edge->DependencyLink);
    LdrpRemoveLink(&Edge->DependencyNode->IncomingDependencies, &Edge->IncomingDependencyLink);
}

static VOID
LdrpLinkDependency(PLDR_DEPENDENCY_RECORD Edge, PLDR_DDAG_NODE Owner, PLDR_DDAG_NODE Target, ULONG_PTR Tag)
{
    Edge->IncomingDependencyNode = (PLDR_DDAG_NODE)((ULONG_PTR)Owner | Tag);
    Edge->DependencyNode = Target;
    LdrpAppendLink(&Owner->Dependencies, &Edge->DependencyLink);
    LdrpAppendLink(&Target->IncomingDependencies, &Edge->IncomingDependencyLink);
}

static BOOLEAN
LdrpNodesLinked(PLDR_DDAG_NODE Owner, PLDR_DDAG_NODE Target)
{
    PSINGLE_LIST_ENTRY Tail = Owner->Dependencies.Tail, Link = Tail;

    if (Owner == Target) return TRUE;
    if (!Tail) return FALSE;
    do
    {
        Link = Link->Next;
        if (CONTAINING_RECORD(Link, LDR_DEPENDENCY_RECORD, DependencyLink)->DependencyNode == Target)
            return TRUE;
    } while (Link != Tail);
    return FALSE;
}

BOOLEAN
LdrpHasDependency(PLDR_DATA_TABLE_ENTRY Owner, PLDR_DATA_TABLE_ENTRY Target)
{
    return LdrpNodesLinked(Owner->DdagNode, Target->DdagNode);
}

BOOLEAN
LdrpInitializeNode(PLDR_DATA_TABLE_ENTRY Entry)
{
    LDRP_NODE *Storage = RtlAllocateHeap(LdrpHeap, HEAP_ZERO_MEMORY, sizeof(*Storage));
    if (!Storage) return FALSE;
    Entry->DdagNode = &Storage->Node;
    InitializeListHead(&Storage->Node.Modules);
    InsertTailList(&Storage->Node.Modules, &Entry->NodeModuleLink);
    Storage->Node.State = LdrModulesMapping;
    Entry->ObsoleteLoadCount = 6;
    Entry->ReferenceCount = 1;
    return TRUE;
}

VOID
LdrpDestroyNode(PLDR_DATA_TABLE_ENTRY Entry)
{
    PLDR_DDAG_NODE Node = Entry->DdagNode;

    RtlAcquireSRWLockExclusive(&LdrpModuleDatatableLock);
    RemoveEntryList(&Entry->NodeModuleLink);
    if (IsListEmpty(&Node->Modules))
    {
        while (Node->Dependencies.Tail || Node->IncomingDependencies.Tail)
        {
            PLDR_DEPENDENCY_RECORD Edge = Node->Dependencies.Tail ?
                CONTAINING_RECORD(Node->Dependencies.Tail, LDR_DEPENDENCY_RECORD, DependencyLink) :
                CONTAINING_RECORD(Node->IncomingDependencies.Tail, LDR_DEPENDENCY_RECORD, IncomingDependencyLink);
            LdrpUnlinkDependency(Edge);
            RtlFreeHeap(LdrpHeap, 0, Edge);
        }
        RtlFreeHeap(LdrpHeap, 0, Node);
    }
    RtlReleaseSRWLockExclusive(&LdrpModuleDatatableLock);
}

static VOID
LdrpSetObsoleteCount(PLDR_DDAG_NODE Node, USHORT Count)
{
    PLIST_ENTRY Link;
    for (Link = Node->Modules.Flink; Link != &Node->Modules; Link = Link->Flink)
        CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, NodeModuleLink)->ObsoleteLoadCount = Count;
}

static VOID
LdrpPinNode(PLDR_DDAG_NODE Node)
{
    PSINGLE_LIST_ENTRY Tail = Node->Dependencies.Tail, Link = Tail;

    if (Node->LoadCount == MAXULONG) return;
    Node->LoadCount = MAXULONG;
    CONTAINING_RECORD(Node->Modules.Flink, LDR_DATA_TABLE_ENTRY, NodeModuleLink)->ObsoleteLoadCount = MAXUSHORT;
    if (Tail)
    {
        do
        {
            Link = Link->Next;
            LdrpPinNode(CONTAINING_RECORD(Link, LDR_DEPENDENCY_RECORD, DependencyLink)->DependencyNode);
        } while (Link != Tail);
    }
}

static VOID
LdrpReleaseNode(PLDR_DDAG_NODE Node)
{
    /* Multiple incoming edges can reach a node already released in this unload. */
    if (Node->State == LdrModulesUnloading) return;
    ASSERT(Node->LoadCount);
    if (Node->LoadCount == MAXULONG) return;
    if (Node->LoadCount == 1 && Node->IncomingDependencies.Tail)
    {
        PSINGLE_LIST_ENTRY Tail = Node->IncomingDependencies.Tail, Link = Tail;
        do
        {
            PLDR_DEPENDENCY_RECORD Edge;
            Link = Link->Next;
            Edge = CONTAINING_RECORD(Link, LDR_DEPENDENCY_RECORD, IncomingDependencyLink);
            if (LdrpDependencyOwner(Edge)->LoadCount) return;
        } while (Link != Tail);
    }
    if (--Node->LoadCount) return;

    Node->State = LdrModulesUnloading;
    LdrpSetObsoleteCount(Node, 0);
    if (Node->Dependencies.Tail)
    {
        PSINGLE_LIST_ENTRY Tail = Node->Dependencies.Tail, Link = Tail;
        do
        {
            Link = Link->Next;
            LdrpReleaseNode(CONTAINING_RECORD(Link, LDR_DEPENDENCY_RECORD, DependencyLink)->DependencyNode);
        } while (Link != Tail);
    }
}

/* The caller holds the module table exclusively. The final release also needs the loader lock. */
BOOLEAN
LdrpAdjustReference(PLDR_DATA_TABLE_ENTRY Entry, BOOLEAN Release, BOOLEAN AllowLast)
{
    PLDR_DDAG_NODE Node = Entry->DdagNode;
    if (Node->LoadCount == MAXULONG) return TRUE;
    if (!Node->LoadCount || Node->State == LdrModulesUnloading) return FALSE;
    if (Release)
    {
        if (Node->LoadCount == 1 && !AllowLast) return FALSE;
        LdrpReleaseNode(Node);
    }
    else
        ++Node->LoadCount;
    return TRUE;
}

BOOLEAN
LdrpReferenceModule(PLDR_DATA_TABLE_ENTRY Entry, BOOLEAN Pin)
{
    BOOLEAN Live;
    RtlAcquireSRWLockExclusive(&LdrpModuleDatatableLock);
    Live = Entry->DdagNode->State >= LdrModulesMapping;
    if (Live && Entry->DdagNode->LoadCount != MAXULONG)
    {
        if (Pin)
        {
            if (!Entry->ProcessStaticImport)
            {
                InterlockedIncrement((PLONG)&Entry->ReferenceCount);
                LdrpPinNode(Entry->DdagNode);
            }
        }
        else
            ++Entry->DdagNode->LoadCount;
    }
    RtlReleaseSRWLockExclusive(&LdrpModuleDatatableLock);
    return Live;
}

/* After adding Owner -> Target, the new component is the intersection of the nodes
 * reachable from Target and those that can reach Owner. */
static VOID
LdrpMarkPaths(PLDR_DDAG_NODE Node, BOOLEAN Reverse, LDRP_NODE **Visited)
{
    LDRP_NODE *Storage = CONTAINING_RECORD(Node, LDRP_NODE, Node);
    PSINGLE_LIST_ENTRY Tail, Link;

    if (Storage->Mark != (ULONG)Reverse) return;
    ++Storage->Mark;
    if (!Reverse)
    {
        Storage->VisitNext = *Visited;
        *Visited = Storage;
    }
    Tail = Reverse ? Node->IncomingDependencies.Tail : Node->Dependencies.Tail;
    if (!Tail) return;
    Link = Tail;
    do
    {
        PLDR_DEPENDENCY_RECORD Edge;
        Link = Link->Next;
        Edge = Reverse ? CONTAINING_RECORD(Link, LDR_DEPENDENCY_RECORD, IncomingDependencyLink) :
                         CONTAINING_RECORD(Link, LDR_DEPENDENCY_RECORD, DependencyLink);
        if (!((ULONG_PTR)Edge->IncomingDependencyNode & 1))
            LdrpMarkPaths(Reverse ? LdrpDependencyOwner(Edge) : Edge->DependencyNode, Reverse, Visited);
    } while (Link != Tail);
}

static VOID
LdrpMergeNode(PLDR_DDAG_NODE Root, PLDR_DDAG_NODE Node)
{
    if (Node->LoadCount == MAXULONG)
        Root->LoadCount = MAXULONG;
    else if (Root->LoadCount != MAXULONG)
        Root->LoadCount += Node->LoadCount;
    if (Root->State > Node->State) Root->State = Node->State;
    while (!IsListEmpty(&Node->Modules))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Node->Modules);
        CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, NodeModuleLink)->DdagNode = Root;
        InsertTailList(&Root->Modules, Link);
    }
    while (Node->Dependencies.Tail || Node->IncomingDependencies.Tail)
    {
        PLDR_DEPENDENCY_RECORD Edge;
        PLDR_DDAG_NODE Source, Destination;
        if (Node->Dependencies.Tail)
            Edge = CONTAINING_RECORD(Node->Dependencies.Tail, LDR_DEPENDENCY_RECORD, DependencyLink);
        else
            Edge = CONTAINING_RECORD(Node->IncomingDependencies.Tail, LDR_DEPENDENCY_RECORD, IncomingDependencyLink);

        Source = LdrpDependencyOwner(Edge);
        if (Source == Node) Source = Root;
        Destination = Edge->DependencyNode == Node ? Root : Edge->DependencyNode;
        LdrpUnlinkDependency(Edge);
        if (LdrpNodesLinked(Source, Destination))
        {
            if (Destination->LoadCount != MAXULONG) --Destination->LoadCount;
            RtlFreeHeap(LdrpHeap, 0, Edge);
        }
        else
            LdrpLinkDependency(Edge, Source, Destination, (ULONG_PTR)Edge->IncomingDependencyNode & 1);
    }
    RtlFreeHeap(LdrpHeap, 0, Node);
}

NTSTATUS
LdrpAddDependency(PLDR_DATA_TABLE_ENTRY OwnerEntry, PLDR_DATA_TABLE_ENTRY TargetEntry, LDR_DLL_LOAD_REASON Reason)
{
    PLDR_DDAG_NODE Owner, Target;
    PLDR_DEPENDENCY_RECORD Edge;
    BOOLEAN Transfer = Reason != LoadReasonStaticDependency;
    BOOLEAN StaticTarget;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlAcquireSRWLockExclusive(&LdrpModuleDatatableLock);
    Owner = OwnerEntry->DdagNode;
    Target = TargetEntry->DdagNode;
    StaticTarget = TargetEntry->ProcessStaticImport && !OwnerEntry->ProcessStaticImport &&
                   OwnerEntry->DllBase != NtCurrentPeb()->ImageBaseAddress;
    if (Target->LoadCount == MAXULONG || StaticTarget || LdrpNodesLinked(Owner, Target))
    {
        if (Transfer) LdrpReleaseNode(Target);
    }
    else
    {
        Edge = RtlAllocateHeap(LdrpHeap, 0, sizeof(*Edge));
        if (!Edge)
            Status = STATUS_NO_MEMORY;
        else
        {
            LdrpLinkDependency(Edge, Owner, Target, Reason == LoadReasonDynamicForwarderDependency);
            if (!Transfer) ++Target->LoadCount;
            if (Reason != LoadReasonDynamicForwarderDependency && Target->Dependencies.Tail)
            {
                LDRP_NODE *Visited = NULL, *Current;
                LdrpMarkPaths(Target, FALSE, &Visited);
                LdrpMarkPaths(Owner, TRUE, &Visited);
                while (Visited)
                {
                    Current = Visited;
                    Visited = Current->VisitNext;
                    if (Current->Mark == 2 && &Current->Node != Target)
                        LdrpMergeNode(Target, &Current->Node);
                    else
                        Current->Mark = 0;
                }
            }
        }
    }
    RtlReleaseSRWLockExclusive(&LdrpModuleDatatableLock);
    return Status;
}
