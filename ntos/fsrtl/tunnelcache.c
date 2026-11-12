/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keeping the name and the identity a deleted file leaves behind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_TUNNEL_TAG 'nTsF'

#define FSRTL_TUNNEL_DEFAULT_ENTRIES 256
#define FSRTL_TUNNEL_DEFAULT_AGE 15

#define FSRTL_TUNNEL_INLINE_DATA 16

typedef struct _TUNNEL_NODE
{
    RTL_SPLAY_LINKS SplayLinks;
    LIST_ENTRY AgeLinks;
    ULONGLONG DirectoryKey;
    UNICODE_STRING ShortName;
    UNICODE_STRING LongName;
    LARGE_INTEGER AddedAt;
    ULONG DataLength;
    PVOID Data;
    BOOLEAN KeyByShortName;
    UCHAR InlineData[FSRTL_TUNNEL_INLINE_DATA];
} TUNNEL_NODE, *PTUNNEL_NODE;

/* GLOBALS ********************************************************************/

ULONG TunnelMaxEntries = FSRTL_TUNNEL_DEFAULT_ENTRIES;
LONGLONG TunnelMaxAge = FSRTL_TUNNEL_DEFAULT_AGE * 10000000LL;
PAGED_LOOKASIDE_LIST TunnelLookasideList;

static BOOLEAN FsRtlTunnelReady = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

static
PUNICODE_STRING
FsRtlpTunnelKeyName(
    _In_ PTUNNEL_NODE Node)
{
    return Node->KeyByShortName ? &Node->ShortName : &Node->LongName;
}

static
FSRTL_COMPARISON_RESULT
FsRtlCompareNodeAndKey(
    _In_ PTUNNEL_NODE Node,
    _In_ ULONGLONG DirectoryKey,
    _In_ PUNICODE_STRING Name)
{
    LONG Result;

    if (Node->DirectoryKey < DirectoryKey) return GreaterThan;
    if (Node->DirectoryKey > DirectoryKey) return LessThan;

    Result = RtlCompareUnicodeString(FsRtlpTunnelKeyName(Node), Name, TRUE);

    if (Result < 0) return GreaterThan;
    if (Result > 0) return LessThan;

    return EqualTo;
}

static
PTUNNEL_NODE
FsRtlpFindTunnelNode(
    _In_ PTUNNEL Cache,
    _In_ ULONGLONG DirectoryKey,
    _In_ PUNICODE_STRING Name,
    _Out_ PTUNNEL_NODE *Parent,
    _Out_ FSRTL_COMPARISON_RESULT *ParentResult)
{
    PRTL_SPLAY_LINKS Links = Cache->Cache;

    *Parent = NULL;
    *ParentResult = EqualTo;

    while (Links != NULL)
    {
        PTUNNEL_NODE Node = CONTAINING_RECORD(Links, TUNNEL_NODE, SplayLinks);
        FSRTL_COMPARISON_RESULT Result;

        Result = FsRtlCompareNodeAndKey(Node, DirectoryKey, Name);

        if (Result == EqualTo)
        {
            Cache->Cache = RtlSplay(Links);
            return Node;
        }

        *Parent = Node;
        *ParentResult = Result;

        Links = (Result == GreaterThan) ? RtlLeftChild(Links)
                                        : RtlRightChild(Links);
    }

    return NULL;
}

static
VOID
FsRtlRemoveNodeFromTunnel(
    _In_ PTUNNEL Cache,
    _In_ PTUNNEL_NODE Node,
    _Inout_ PLIST_ENTRY FreeList)
{
    Cache->Cache = RtlDelete(&Node->SplayLinks);
    RemoveEntryList(&Node->AgeLinks);
    Cache->NumEntries--;

    InsertTailList(FreeList, &Node->AgeLinks);
}

static
VOID
FsRtlFreeTunnelNode(
    _In_ PTUNNEL_NODE Node)
{
    if (Node->LongName.Buffer != NULL)
    {
        ExFreePoolWithTag(Node->LongName.Buffer, FSRTL_TUNNEL_TAG);
    }

    if (Node->Data != Node->InlineData)
    {
        ExFreePoolWithTag(Node->Data, FSRTL_TUNNEL_TAG);
    }

    ExFreeToPagedLookasideList(&TunnelLookasideList, Node);
}

static
VOID
FsRtlEmptyFreePoolList(
    _Inout_ PLIST_ENTRY FreeList)
{
    while (!IsListEmpty(FreeList))
    {
        PTUNNEL_NODE Node = CONTAINING_RECORD(RemoveHeadList(FreeList),
                                              TUNNEL_NODE,
                                              AgeLinks);

        FsRtlFreeTunnelNode(Node);
    }
}

static
VOID
FsRtlPruneTunnelCache(
    _In_ PTUNNEL Cache,
    _Inout_ PLIST_ENTRY FreeList)
{
    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now);

    while (!IsListEmpty(&Cache->TimerQueue))
    {
        PTUNNEL_NODE Node = CONTAINING_RECORD(Cache->TimerQueue.Flink,
                                              TUNNEL_NODE,
                                              AgeLinks);

        if ((Cache->NumEntries <= TunnelMaxEntries) &&
            ((Now.QuadPart - Node->AddedAt.QuadPart) < TunnelMaxAge))
        {
            break;
        }

        FsRtlRemoveNodeFromTunnel(Cache, Node, FreeList);
    }
}

static
PTUNNEL_NODE
FsRtlpAllocateTunnelNode(
    _In_ ULONGLONG DirectoryKey,
    _In_ PUNICODE_STRING ShortName,
    _In_ PUNICODE_STRING LongName,
    _In_ BOOLEAN KeyByShortName,
    _In_ ULONG DataLength,
    _In_reads_bytes_(DataLength) PVOID Data)
{
    PTUNNEL_NODE Node;
    ULONG NameBytes;

    Node = ExAllocateFromPagedLookasideList(&TunnelLookasideList);
    if (Node == NULL) return NULL;

    RtlZeroMemory(Node, sizeof(*Node));

    NameBytes = (ULONG)ShortName->Length + (ULONG)LongName->Length;

    if (NameBytes != 0)
    {
        Node->LongName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                      NameBytes,
                                                      FSRTL_TUNNEL_TAG);

        if (Node->LongName.Buffer == NULL)
        {
            ExFreeToPagedLookasideList(&TunnelLookasideList, Node);
            return NULL;
        }

        Node->ShortName.Buffer =
            (PWSTR)((PUCHAR)Node->LongName.Buffer + LongName->Length);
    }

    Node->LongName.Length = LongName->Length;
    Node->LongName.MaximumLength = LongName->Length;
    Node->ShortName.Length = ShortName->Length;
    Node->ShortName.MaximumLength = ShortName->Length;

    if (LongName->Length != 0)
    {
        RtlCopyMemory(Node->LongName.Buffer,
                      LongName->Buffer,
                      LongName->Length);
    }

    if (ShortName->Length != 0)
    {
        RtlCopyMemory(Node->ShortName.Buffer,
                      ShortName->Buffer,
                      ShortName->Length);
    }

    if (DataLength > FSRTL_TUNNEL_INLINE_DATA)
    {
        Node->Data = ExAllocatePoolWithTag(PagedPool,
                                           DataLength,
                                           FSRTL_TUNNEL_TAG);

        if (Node->Data == NULL)
        {
            if (Node->LongName.Buffer != NULL)
            {
                ExFreePoolWithTag(Node->LongName.Buffer, FSRTL_TUNNEL_TAG);
            }

            ExFreeToPagedLookasideList(&TunnelLookasideList, Node);
            return NULL;
        }
    }
    else
    {
        Node->Data = Node->InlineData;
    }

    Node->DataLength = DataLength;
    Node->DirectoryKey = DirectoryKey;
    Node->KeyByShortName = KeyByShortName;

    if (DataLength != 0) RtlCopyMemory(Node->Data, Data, DataLength);

    KeQuerySystemTime(&Node->AddedAt);
    RtlInitializeSplayLinks(&Node->SplayLinks);

    return Node;
}

static
NTSTATUS
FsRtlGetTunnelParameterValue(
    _In_ PCWSTR ValueName,
    _Out_ PULONG Value)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\FileSystem");
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    HANDLE Key;
    ULONG Length;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, ValueName);
    Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;

    Status = ZwQueryValueKey(Key,
                             &Name,
                             KeyValuePartialInformation,
                             Information,
                             sizeof(Buffer),
                             &Length);

    ZwClose(Key);

    if (!NT_SUCCESS(Status)) return Status;

    if ((Information->Type != REG_DWORD) ||
        (Information->DataLength != sizeof(ULONG)))
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    RtlCopyMemory(Value, Information->Data, sizeof(ULONG));

    return STATUS_SUCCESS;
}

/* INIT FUNCTIONS *************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeTunnels(VOID)
{
    ULONG Value;

    if (NT_SUCCESS(FsRtlGetTunnelParameterValue(L"MaximumTunnelEntries",
                                                &Value)))
    {
        TunnelMaxEntries = Value;
    }

    if (NT_SUCCESS(FsRtlGetTunnelParameterValue(
                       L"MaximumTunnelEntryAgeInSeconds",
                       &Value)))
    {
        TunnelMaxAge = (LONGLONG)Value * 10000000LL;
    }

    if (TunnelMaxEntries > MAXUSHORT) TunnelMaxEntries = MAXUSHORT;

    if (TunnelMaxEntries == 0) return;

    ExInitializePagedLookasideList(&TunnelLookasideList,
                                   NULL,
                                   NULL,
                                   0,
                                   sizeof(TUNNEL_NODE),
                                   FSRTL_TUNNEL_TAG,
                                   0);

    FsRtlTunnelReady = TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlInitializeTunnelCache(
    _In_ PTUNNEL Cache)
{
    PAGED_CODE();

    ExInitializeFastMutex(&Cache->Mutex);
    Cache->Cache = NULL;
    InitializeListHead(&Cache->TimerQueue);
    Cache->NumEntries = 0;
}

VOID
NTAPI
FsRtlAddToTunnelCache(
    _In_ PTUNNEL Cache,
    _In_ ULONGLONG DirectoryKey,
    _In_ PUNICODE_STRING ShortName,
    _In_ PUNICODE_STRING LongName,
    _In_ BOOLEAN KeyByShortName,
    _In_ ULONG DataLength,
    _In_reads_bytes_(DataLength) PVOID Data)
{
    FSRTL_COMPARISON_RESULT Result;
    PUNICODE_STRING Key;
    PTUNNEL_NODE Parent;
    PTUNNEL_NODE Existing;
    PTUNNEL_NODE Node;
    LIST_ENTRY FreeList;

    PAGED_CODE();

    if (!FsRtlTunnelReady) return;

    Node = FsRtlpAllocateTunnelNode(DirectoryKey,
                                    ShortName,
                                    LongName,
                                    KeyByShortName,
                                    DataLength,
                                    Data);

    if (Node == NULL) return;

    InitializeListHead(&FreeList);
    Key = FsRtlpTunnelKeyName(Node);

    ExAcquireFastMutex(&Cache->Mutex);

    Existing = FsRtlpFindTunnelNode(Cache,
                                    DirectoryKey,
                                    Key,
                                    &Parent,
                                    &Result);

    if (Existing != NULL)
    {
        FsRtlRemoveNodeFromTunnel(Cache, Existing, &FreeList);

        Existing = FsRtlpFindTunnelNode(Cache,
                                        DirectoryKey,
                                        Key,
                                        &Parent,
                                        &Result);
    }

    if (Parent == NULL)
    {
        Cache->Cache = &Node->SplayLinks;
    }
    else
    {
        if (Result == GreaterThan)
        {
            RtlInsertAsLeftChild(&Parent->SplayLinks, &Node->SplayLinks);
        }
        else
        {
            RtlInsertAsRightChild(&Parent->SplayLinks, &Node->SplayLinks);
        }

        Cache->Cache = RtlSplay(&Node->SplayLinks);
    }

    InsertTailList(&Cache->TimerQueue, &Node->AgeLinks);
    Cache->NumEntries++;

    FsRtlPruneTunnelCache(Cache, &FreeList);

    ExReleaseFastMutex(&Cache->Mutex);

    FsRtlEmptyFreePoolList(&FreeList);
}

BOOLEAN
NTAPI
FsRtlFindInTunnelCache(
    _In_ PTUNNEL Cache,
    _In_ ULONGLONG DirectoryKey,
    _In_ PUNICODE_STRING Name,
    _Out_ PUNICODE_STRING ShortName,
    _Out_ PUNICODE_STRING LongName,
    _Inout_ PULONG DataLength,
    _Out_writes_bytes_to_(*DataLength, *DataLength) PVOID Data)
{
    FSRTL_COMPARISON_RESULT Result;
    PTUNNEL_NODE Parent;
    PTUNNEL_NODE Node;
    LIST_ENTRY FreeList;
    BOOLEAN Found = FALSE;

    PAGED_CODE();

    if (!FsRtlTunnelReady) return FALSE;

    InitializeListHead(&FreeList);

    ExAcquireFastMutex(&Cache->Mutex);

    FsRtlPruneTunnelCache(Cache, &FreeList);

    Node = FsRtlpFindTunnelNode(Cache,
                                DirectoryKey,
                                Name,
                                &Parent,
                                &Result);

    if ((Node != NULL) && (Node->DataLength <= *DataLength) &&
        (Node->ShortName.Length <= ShortName->MaximumLength))
    {
        if (Node->LongName.Length > LongName->MaximumLength)
        {
            PWSTR Grown = ExAllocatePoolWithTag(PagedPool,
                                                Node->LongName.Length,
                                                FSRTL_TUNNEL_TAG);

            if (Grown != NULL)
            {
                LongName->Buffer = Grown;
                LongName->MaximumLength = Node->LongName.Length;
            }
        }

        if (Node->LongName.Length <= LongName->MaximumLength)
        {
            RtlCopyMemory(ShortName->Buffer,
                          Node->ShortName.Buffer,
                          Node->ShortName.Length);
            ShortName->Length = Node->ShortName.Length;

            RtlCopyMemory(LongName->Buffer,
                          Node->LongName.Buffer,
                          Node->LongName.Length);
            LongName->Length = Node->LongName.Length;

            if (Node->DataLength != 0)
            {
                RtlCopyMemory(Data, Node->Data, Node->DataLength);
            }

            *DataLength = Node->DataLength;

            Found = TRUE;
        }
    }

    ExReleaseFastMutex(&Cache->Mutex);

    FsRtlEmptyFreePoolList(&FreeList);

    return Found;
}

VOID
NTAPI
FsRtlDeleteKeyFromTunnelCache(
    _In_ PTUNNEL Cache,
    _In_ ULONGLONG DirectoryKey)
{
    LIST_ENTRY FreeList;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!FsRtlTunnelReady) return;

    InitializeListHead(&FreeList);

    ExAcquireFastMutex(&Cache->Mutex);

    Entry = Cache->TimerQueue.Flink;

    while (Entry != &Cache->TimerQueue)
    {
        PTUNNEL_NODE Node = CONTAINING_RECORD(Entry, TUNNEL_NODE, AgeLinks);

        Entry = Entry->Flink;

        if (Node->DirectoryKey == DirectoryKey)
        {
            FsRtlRemoveNodeFromTunnel(Cache, Node, &FreeList);
        }
    }

    ExReleaseFastMutex(&Cache->Mutex);

    FsRtlEmptyFreePoolList(&FreeList);
}

VOID
NTAPI
FsRtlDeleteTunnelCache(
    _In_ PTUNNEL Cache)
{
    LIST_ENTRY FreeList;

    PAGED_CODE();

    if (!FsRtlTunnelReady) return;

    InitializeListHead(&FreeList);

    ExAcquireFastMutex(&Cache->Mutex);

    while (!IsListEmpty(&Cache->TimerQueue))
    {
        PTUNNEL_NODE Node = CONTAINING_RECORD(Cache->TimerQueue.Flink,
                                              TUNNEL_NODE,
                                              AgeLinks);

        FsRtlRemoveNodeFromTunnel(Cache, Node, &FreeList);
    }

    ExReleaseFastMutex(&Cache->Mutex);

    FsRtlEmptyFreePoolList(&FreeList);
}

/* EOF */
