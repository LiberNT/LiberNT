/*
 * PROJECT:     LiberNT Smart Card Service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Smart card resource manager calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "scardsvr.h"

#define NDEBUG
#include <debug.h>

#define SC_PNP_NOTIFICATION "\\\\?PnP?\\Notification"
#define SC_DEFAULT_GROUP    "SCard$DefaultReaders"
#define SC_ALL_GROUP        "SCard$AllReaders"

static ULONGLONG ScNextId = 0x5C000001;

void __RPC_FAR * __RPC_USER
midl_user_allocate(SIZE_T Length)
{
    return HeapAlloc(GetProcessHeap(), 0, Length);
}

void __RPC_USER
midl_user_free(void __RPC_FAR *Pointer)
{
    HeapFree(GetProcessHeap(), 0, Pointer);
}

DWORD
ScStartRpcServer(VOID)
{
    RPC_STATUS Status;

    Status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)SCARDSVR_ENDPOINT,
                                    NULL);
    if (Status != RPC_S_OK)
        return Status;
    Status = RpcServerRegisterIf(scardsvr_v1_0_s_ifspec, NULL, NULL);
    if (Status != RPC_S_OK)
        return Status;
    return RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
}

static
SC_CONTEXT *
ScFindContext(
    _In_ SC_SESSION *Session,
    _In_ ULONGLONG Id)
{
    PLIST_ENTRY Entry;

    if (!Session)
        return NULL;
    for (Entry = Session->Contexts.Flink; Entry != &Session->Contexts; Entry = Entry->Flink)
    {
        SC_CONTEXT *Context = CONTAINING_RECORD(Entry, SC_CONTEXT, Entry);

        if (Context->Id == Id && !Context->Released)
            return Context;
    }
    return NULL;
}

static
SC_CARD *
ScFindCard(
    _In_ SC_SESSION *Session,
    _In_ ULONGLONG Id)
{
    PLIST_ENTRY Entry, CardEntry;

    if (!Session)
        return NULL;
    for (Entry = Session->Contexts.Flink; Entry != &Session->Contexts; Entry = Entry->Flink)
    {
        SC_CONTEXT *Context = CONTAINING_RECORD(Entry, SC_CONTEXT, Entry);

        if (Context->Released)
            continue;
        for (CardEntry = Context->Cards.Flink; CardEntry != &Context->Cards; CardEntry = CardEntry->Flink)
        {
            SC_CARD *Card = CONTAINING_RECORD(CardEntry, SC_CARD, Entry);

            if (Card->Id == Id)
                return Card;
        }
    }
    return NULL;
}

static
SC_READER *
ScFindReader(
    _In_ const char *Name)
{
    PLIST_ENTRY Entry;

    for (Entry = ScReaders.Flink; Entry != &ScReaders; Entry = Entry->Flink)
    {
        SC_READER *Reader = CONTAINING_RECORD(Entry, SC_READER, Entry);

        if (!_stricmp(Reader->Name, Name))
            return Reader;
    }
    return NULL;
}

static
ULONG
ScCountReaders(VOID)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = ScReaders.Flink; Entry != &ScReaders; Entry = Entry->Flink)
        ++Count;
    return Count;
}

static
LONG
ScCheckCard(
    _In_ SC_CARD *Card)
{
    SC_READER *Reader = Card->Reader;

    if (Reader->Removed)
        return SCARD_E_READER_UNAVAILABLE;
    if (Card->ShareMode == SCARD_SHARE_DIRECT)
        return SCARD_S_SUCCESS;
    if (Card->Events != Reader->Events || Reader->State <= SCARD_ABSENT)
        return SCARD_W_REMOVED_CARD;
    if (Card->Resets != Reader->Resets)
        return SCARD_W_RESET_CARD;
    return SCARD_S_SUCCESS;
}

static
VOID
ScDetachCard(
    _Inout_ SC_CARD *Card)
{
    SC_READER *Reader = Card->Reader;

    if (Reader->Transaction == Card)
        Reader->Transaction = NULL;
    if (Reader->Exclusive == Card)
        Reader->Exclusive = NULL;
    else if (Card->ShareMode == SCARD_SHARE_SHARED)
        --Reader->Shared;
    RemoveEntryList(&Card->Entry);
    WakeAllConditionVariable(&ScChanged);
}

static
VOID
ScFreeCard(
    _Inout_ SC_CARD *Card)
{
    ScDereferenceReader(Card->Reader);
    HeapFree(GetProcessHeap(), 0, Card);
}

static
VOID
ScReleaseContextLocked(
    _Inout_ SC_CONTEXT *Context)
{
    Context->Released = TRUE;
    while (!IsListEmpty(&Context->Cards))
    {
        SC_CARD *Card = CONTAINING_RECORD(Context->Cards.Flink, SC_CARD, Entry);

        ScDetachCard(Card);
        LeaveCriticalSection(&ScLock);
        ScFreeCard(Card);
        EnterCriticalSection(&ScLock);
    }
    RemoveEntryList(&Context->Entry);
    WakeAllConditionVariable(&ScChanged);
    if (!--Context->References)
        HeapFree(GetProcessHeap(), 0, Context);
}

LONG
ScOpenSession(
    handle_t Binding,
    SCARDSVR_SESSION *Session)
{
    SC_SESSION *New;

    UNREFERENCED_PARAMETER(Binding);
    New = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*New));
    if (!New)
        return SCARD_E_NO_MEMORY;
    InitializeListHead(&New->Contexts);
    *Session = New;
    return SCARD_S_SUCCESS;
}

static
VOID
ScDestroySession(
    _In_ SC_SESSION *Session)
{
    EnterCriticalSection(&ScLock);
    while (!IsListEmpty(&Session->Contexts))
        ScReleaseContextLocked(CONTAINING_RECORD(Session->Contexts.Flink, SC_CONTEXT, Entry));
    LeaveCriticalSection(&ScLock);
    HeapFree(GetProcessHeap(), 0, Session);
}

LONG
ScCloseSession(
    SCARDSVR_SESSION *Session)
{
    if (*Session)
        ScDestroySession(*Session);
    *Session = NULL;
    return SCARD_S_SUCCESS;
}

void __RPC_USER
SCARDSVR_SESSION_rundown(SCARDSVR_SESSION Session)
{
    if (Session)
        ScDestroySession(Session);
}

LONG
ScEstablishContext(
    SCARDSVR_SESSION Session,
    DWORD Scope,
    ULONGLONG *Context)
{
    SC_SESSION *Owner = Session;
    SC_CONTEXT *New;

    *Context = 0;
    if (Scope != SCARD_SCOPE_USER && Scope != SCARD_SCOPE_TERMINAL && Scope != SCARD_SCOPE_SYSTEM)
        return SCARD_E_INVALID_VALUE;
    New = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*New));
    if (!New)
        return SCARD_E_NO_MEMORY;
    InitializeListHead(&New->Cards);
    New->Session = Owner;
    New->References = 1;
    EnterCriticalSection(&ScLock);
    New->Id = ScNextId++;
    InsertTailList(&Owner->Contexts, &New->Entry);
    *Context = New->Id;
    LeaveCriticalSection(&ScLock);
    return SCARD_S_SUCCESS;
}

LONG
ScReleaseContext(
    SCARDSVR_SESSION Session,
    ULONGLONG Id)
{
    SC_CONTEXT *Context;

    EnterCriticalSection(&ScLock);
    Context = ScFindContext(Session, Id);
    if (Context)
        ScReleaseContextLocked(Context);
    LeaveCriticalSection(&ScLock);
    return Context ? SCARD_S_SUCCESS : ERROR_INVALID_HANDLE;
}

LONG
ScIsValidContext(
    SCARDSVR_SESSION Session,
    ULONGLONG Id)
{
    BOOL Valid;

    EnterCriticalSection(&ScLock);
    Valid = ScFindContext(Session, Id) != NULL;
    LeaveCriticalSection(&ScLock);
    return Valid ? SCARD_S_SUCCESS : ERROR_INVALID_HANDLE;
}

LONG
ScCancel(
    SCARDSVR_SESSION Session,
    ULONGLONG Id)
{
    SC_CONTEXT *Context;

    EnterCriticalSection(&ScLock);
    Context = ScFindContext(Session, Id);
    if (Context)
    {
        ++Context->Cancels;
        WakeAllConditionVariable(&ScChanged);
    }
    LeaveCriticalSection(&ScLock);
    return Context ? SCARD_S_SUCCESS : ERROR_INVALID_HANDLE;
}

static
DWORD
ScReaderEventState(
    _In_ SC_READER *Reader)
{
    DWORD State;

    if (Reader->State <= SCARD_ABSENT)
    {
        State = SCARD_STATE_EMPTY;
    }
    else
    {
        State = SCARD_STATE_PRESENT;
        if (Reader->Mute)
            State |= SCARD_STATE_MUTE;
        else if (Reader->State < SCARD_NEGOTIABLE)
            State |= SCARD_STATE_UNPOWERED;
        if (Reader->Exclusive && Reader->Exclusive->ShareMode == SCARD_SHARE_EXCLUSIVE)
            State |= SCARD_STATE_EXCLUSIVE;
        else if (Reader->Exclusive || Reader->Shared)
            State |= SCARD_STATE_INUSE;
    }
    return State | ((Reader->Events & 0xFFFF) << 16);
}

static
BOOL
ScEvaluateStates(
    _In_ DWORD Count,
    _In_reads_(Count) const SCARDSVR_READER_IN *In,
    _Out_writes_(Count) SCARDSVR_READER_OUT *Out)
{
    SC_READER *Reader;
    DWORD Index, Current, Event;
    BOOL Any = FALSE;

    for (Index = 0; Index < Count; ++Index)
    {
        Current = In[Index].CurrentState;
        Out[Index].AtrLength = 0;
        if (Current & SCARD_STATE_IGNORE)
        {
            Out[Index].EventState = SCARD_STATE_IGNORE;
            continue;
        }
        if (!_stricmp(In[Index].Reader, SC_PNP_NOTIFICATION))
        {
            Event = ScCountReaders() << 16;
            if ((Current >> 16) != (Event >> 16))
                Event |= SCARD_STATE_CHANGED;
        }
        else if (!(Reader = ScFindReader(In[Index].Reader)))
        {
            Event = SCARD_STATE_UNKNOWN | SCARD_STATE_IGNORE;
            if (!(Current & SCARD_STATE_UNKNOWN))
                Event |= SCARD_STATE_CHANGED;
        }
        else
        {
            Event = ScReaderEventState(Reader);
            if (Reader->State > SCARD_ABSENT && Reader->AtrLength)
            {
                Out[Index].AtrLength = Reader->AtrLength;
                memcpy(Out[Index].Atr, Reader->Atr, Reader->AtrLength);
            }
            if (Current == SCARD_STATE_UNAWARE ||
                ((Current ^ Event) & 0xFFFF & ~(SCARD_STATE_CHANGED | SCARD_STATE_IGNORE)) ||
                ((Current >> 16) && (Current >> 16) != (Event >> 16)))
            {
                Event |= SCARD_STATE_CHANGED;
            }
        }
        Out[Index].EventState = Event;
        if (Event & SCARD_STATE_CHANGED)
            Any = TRUE;
    }
    return Any;
}

LONG
ScGetStatusChange(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD Timeout,
    DWORD Count,
    SCARDSVR_READER_IN *In,
    SCARDSVR_READER_OUT *Out)
{
    SC_CONTEXT *Context;
    ULONGLONG Deadline = Timeout == INFINITE ? 0 : GetTickCount64() + Timeout, Now;
    LONG Cancels, Result;

    EnterCriticalSection(&ScLock);
    Context = ScFindContext(Session, Id);
    if (!Context)
    {
        LeaveCriticalSection(&ScLock);
        return ERROR_INVALID_HANDLE;
    }
    ++Context->References;
    Cancels = Context->Cancels;
    for (;;)
    {
        if (ScEvaluateStates(Count, In, Out))
        {
            Result = SCARD_S_SUCCESS;
            break;
        }
        if (Context->Cancels != Cancels || Context->Released)
        {
            Result = SCARD_E_CANCELLED;
            break;
        }
        Now = GetTickCount64();
        if (Timeout != INFINITE && Now >= Deadline)
        {
            Result = SCARD_E_TIMEOUT;
            break;
        }
        SleepConditionVariableCS(&ScChanged, &ScLock, Timeout == INFINITE ? INFINITE : (DWORD)(Deadline - Now));
    }
    if (!--Context->References)
        HeapFree(GetProcessHeap(), 0, Context);
    LeaveCriticalSection(&ScLock);
    return Result;
}

static
BOOL
ScGroupListed(
    _In_opt_ const char *Groups)
{
    if (!Groups || !*Groups)
        return TRUE;
    for (; *Groups; Groups += strlen(Groups) + 1)
    {
        if (!_stricmp(Groups, SC_DEFAULT_GROUP) || !_stricmp(Groups, SC_ALL_GROUP))
            return TRUE;
    }
    return FALSE;
}

LONG
ScListReaders(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    char *Groups,
    DWORD Size,
    BYTE *Readers,
    DWORD *Returned)
{
    PLIST_ENTRY Entry;
    DWORD Length = 0, Name;
    LONG Result = SCARD_S_SUCCESS;

    *Returned = 0;
    EnterCriticalSection(&ScLock);
    if (!ScFindContext(Session, Id))
    {
        LeaveCriticalSection(&ScLock);
        return ERROR_INVALID_HANDLE;
    }
    if (ScGroupListed(Groups))
    {
        for (Entry = ScReaders.Flink; Entry != &ScReaders; Entry = Entry->Flink)
        {
            SC_READER *Reader = CONTAINING_RECORD(Entry, SC_READER, Entry);

            Name = (DWORD)strlen(Reader->Name) + 1;
            if (Length + Name + 1 > Size)
            {
                Result = SCARD_E_INSUFFICIENT_BUFFER;
                break;
            }
            memcpy(Readers + Length, Reader->Name, Name);
            Length += Name;
        }
    }
    LeaveCriticalSection(&ScLock);
    if (Result != SCARD_S_SUCCESS)
        return Result;
    if (!Length)
        return SCARD_E_NO_READERS_AVAILABLE;
    Readers[Length++] = 0;
    *Returned = Length;
    return SCARD_S_SUCCESS;
}

LONG
ScListReaderGroups(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD Size,
    BYTE *Groups,
    DWORD *Returned)
{
    static const char Default[] = SC_DEFAULT_GROUP "\0";
    BOOL Valid;

    *Returned = 0;
    EnterCriticalSection(&ScLock);
    Valid = ScFindContext(Session, Id) != NULL;
    LeaveCriticalSection(&ScLock);
    if (!Valid)
        return ERROR_INVALID_HANDLE;
    if (Size < sizeof(Default))
        return SCARD_E_INSUFFICIENT_BUFFER;
    memcpy(Groups, Default, sizeof(Default));
    *Returned = sizeof(Default);
    return SCARD_S_SUCCESS;
}

static
LONG
ScClaimReader(
    _Inout_ SC_READER *Reader,
    _In_ SC_CARD *Card,
    _In_ DWORD ShareMode)
{
    if (Reader->Exclusive && Reader->Exclusive != Card)
        return SCARD_E_SHARING_VIOLATION;
    if (ShareMode != SCARD_SHARE_SHARED && Reader->Shared > (Card->ShareMode == SCARD_SHARE_SHARED ? 1u : 0u))
        return SCARD_E_SHARING_VIOLATION;
    if (Card->ShareMode == SCARD_SHARE_SHARED)
        --Reader->Shared;
    else if (Reader->Exclusive == Card)
        Reader->Exclusive = NULL;
    Card->ShareMode = ShareMode;
    if (ShareMode == SCARD_SHARE_SHARED)
        ++Reader->Shared;
    else
        Reader->Exclusive = Card;
    return SCARD_S_SUCCESS;
}

LONG
ScConnect(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    char *Name,
    DWORD ShareMode,
    DWORD Preferred,
    ULONGLONG *Handle,
    DWORD *Protocol)
{
    SC_CONTEXT *Context;
    SC_READER *Reader;
    SC_CARD *Card;
    DWORD Selected = SCARD_PROTOCOL_UNDEFINED;
    LONG Result;

    *Handle = 0;
    *Protocol = 0;
    if (ShareMode != SCARD_SHARE_SHARED && ShareMode != SCARD_SHARE_EXCLUSIVE && ShareMode != SCARD_SHARE_DIRECT)
        return SCARD_E_INVALID_VALUE;
    if (ShareMode != SCARD_SHARE_DIRECT && !(Preferred & (SCARD_PROTOCOL_Tx | SCARD_PROTOCOL_RAW)))
        return SCARD_E_INVALID_VALUE;
    Card = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Card));
    if (!Card)
        return SCARD_E_NO_MEMORY;

    EnterCriticalSection(&ScLock);
    Context = ScFindContext(Session, Id);
    Reader = Context ? ScFindReader(Name) : NULL;
    if (!Context || !Reader)
    {
        LeaveCriticalSection(&ScLock);
        HeapFree(GetProcessHeap(), 0, Card);
        return Context ? SCARD_E_UNKNOWN_READER : ERROR_INVALID_HANDLE;
    }
    if (Reader->Exclusive || (ShareMode != SCARD_SHARE_SHARED && Reader->Shared))
        Result = SCARD_E_SHARING_VIOLATION;
    else if (ShareMode != SCARD_SHARE_DIRECT && Reader->State <= SCARD_ABSENT)
        Result = SCARD_W_REMOVED_CARD;
    else if (ShareMode != SCARD_SHARE_DIRECT && Reader->Mute)
        Result = SCARD_W_UNRESPONSIVE_CARD;
    else
        Result = SCARD_S_SUCCESS;
    if (Result == SCARD_S_SUCCESS)
        ScReferenceReader(Reader);
    LeaveCriticalSection(&ScLock);
    if (Result != SCARD_S_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Card);
        return Result;
    }

    if (ShareMode != SCARD_SHARE_DIRECT)
    {
        EnterCriticalSection(&Reader->IoLock);
        Result = ScSetProtocol(Reader, Preferred, &Selected);
        LeaveCriticalSection(&Reader->IoLock);
    }

    EnterCriticalSection(&ScLock);
    Context = ScFindContext(Session, Id);
    if (Result == SCARD_S_SUCCESS && (!Context || Reader->Removed))
        Result = Context ? SCARD_E_READER_UNAVAILABLE : ERROR_INVALID_HANDLE;
    if (Result == SCARD_S_SUCCESS && (Reader->Exclusive || (ShareMode != SCARD_SHARE_SHARED && Reader->Shared)))
        Result = SCARD_E_SHARING_VIOLATION;
    if (Result == SCARD_S_SUCCESS)
    {
        Card->Id = ScNextId++;
        Card->Context = Context;
        Card->Reader = Reader;
        Card->Protocol = Selected;
        Card->Events = Reader->Events;
        Card->Resets = Reader->Resets;
        Card->ShareMode = ShareMode;
        if (ShareMode == SCARD_SHARE_SHARED)
            ++Reader->Shared;
        else
            Reader->Exclusive = Card;
        InsertTailList(&Context->Cards, &Card->Entry);
        WakeAllConditionVariable(&ScChanged);
        *Handle = Card->Id;
        *Protocol = Selected;
    }
    LeaveCriticalSection(&ScLock);
    if (Result != SCARD_S_SUCCESS)
    {
        ScDereferenceReader(Reader);
        HeapFree(GetProcessHeap(), 0, Card);
    }
    return Result;
}

static
LONG
ScAcquireCard(
    _In_ SC_SESSION *Session,
    _In_ ULONGLONG Id,
    _Out_ SC_CARD **Card,
    _In_ BOOL Check)
{
    LONG Result = SCARD_S_SUCCESS;

    EnterCriticalSection(&ScLock);
    *Card = ScFindCard(Session, Id);
    if (!*Card)
        Result = ERROR_INVALID_HANDLE;
    else if (Check)
        Result = ScCheckCard(*Card);
    if (Result == SCARD_S_SUCCESS)
        ScReferenceReader((*Card)->Reader);
    LeaveCriticalSection(&ScLock);
    return Result;
}

LONG
ScReconnect(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD ShareMode,
    DWORD Preferred,
    DWORD Initialization,
    DWORD *Protocol)
{
    SC_CARD *Card;
    SC_READER *Reader;
    DWORD Selected = SCARD_PROTOCOL_UNDEFINED;
    LONG Result;

    *Protocol = 0;
    if (ShareMode != SCARD_SHARE_SHARED && ShareMode != SCARD_SHARE_EXCLUSIVE && ShareMode != SCARD_SHARE_DIRECT)
        return SCARD_E_INVALID_VALUE;
    Result = ScAcquireCard(Session, Id, &Card, FALSE);
    if (Result != SCARD_S_SUCCESS)
        return Result;
    Reader = Card->Reader;
    EnterCriticalSection(&ScLock);
    Result = Reader->Removed ? SCARD_E_READER_UNAVAILABLE : ScClaimReader(Reader, Card, ShareMode);
    LeaveCriticalSection(&ScLock);
    if (Result == SCARD_S_SUCCESS && ShareMode != SCARD_SHARE_DIRECT)
    {
        EnterCriticalSection(&Reader->IoLock);
        if (Initialization != SCARD_LEAVE_CARD)
            Result = ScPowerCard(Reader, Initialization == SCARD_UNPOWER_CARD ? SCARD_UNPOWER_CARD : SCARD_RESET_CARD);
        if (Result == SCARD_S_SUCCESS)
            Result = ScSetProtocol(Reader, Preferred, &Selected);
        LeaveCriticalSection(&Reader->IoLock);
    }
    if (Result == SCARD_S_SUCCESS)
    {
        EnterCriticalSection(&ScLock);
        Card->Protocol = Selected;
        Card->Events = Reader->Events;
        Card->Resets = Reader->Resets;
        LeaveCriticalSection(&ScLock);
        *Protocol = Selected;
    }
    ScDereferenceReader(Reader);
    return Result;
}

static
LONG
ScApplyDisposition(
    _Inout_ SC_READER *Reader,
    _In_ DWORD Disposition)
{
    LONG Result;

    if (Disposition == SCARD_LEAVE_CARD || Reader->State <= SCARD_ABSENT)
        return SCARD_S_SUCCESS;
    EnterCriticalSection(&Reader->IoLock);
    Result = ScPowerCard(Reader, Disposition);
    LeaveCriticalSection(&Reader->IoLock);
    return Result;
}

LONG
ScDisconnect(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD Disposition)
{
    SC_CARD *Card;
    SC_READER *Reader;

    if (Disposition > SCARD_EJECT_CARD)
        return SCARD_E_INVALID_VALUE;
    EnterCriticalSection(&ScLock);
    Card = ScFindCard(Session, Id);
    if (!Card)
    {
        LeaveCriticalSection(&ScLock);
        return ERROR_INVALID_HANDLE;
    }
    Reader = Card->Reader;
    ScDetachCard(Card);
    LeaveCriticalSection(&ScLock);
    if (!Reader->Removed && Card->ShareMode != SCARD_SHARE_DIRECT)
        ScApplyDisposition(Reader, Disposition);
    ScFreeCard(Card);
    return SCARD_S_SUCCESS;
}

LONG
ScBeginTransaction(
    SCARDSVR_SESSION Session,
    ULONGLONG Id)
{
    SC_CARD *Card;
    LONG Result;

    EnterCriticalSection(&ScLock);
    for (;;)
    {
        Card = ScFindCard(Session, Id);
        if (!Card)
        {
            Result = ERROR_INVALID_HANDLE;
            break;
        }
        Result = ScCheckCard(Card);
        if (Result != SCARD_S_SUCCESS)
            break;
        if (!Card->Reader->Transaction || Card->Reader->Transaction == Card)
        {
            Card->Reader->Transaction = Card;
            break;
        }
        SleepConditionVariableCS(&ScChanged, &ScLock, INFINITE);
    }
    LeaveCriticalSection(&ScLock);
    return Result;
}

LONG
ScEndTransaction(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD Disposition)
{
    SC_CARD *Card;
    SC_READER *Reader;
    LONG Result = SCARD_S_SUCCESS;

    if (Disposition > SCARD_EJECT_CARD)
        return SCARD_E_INVALID_VALUE;
    EnterCriticalSection(&ScLock);
    Card = ScFindCard(Session, Id);
    if (!Card || Card->Reader->Transaction != Card)
    {
        LeaveCriticalSection(&ScLock);
        return Card ? SCARD_E_NOT_TRANSACTED : ERROR_INVALID_HANDLE;
    }
    Reader = Card->Reader;
    ScReferenceReader(Reader);
    LeaveCriticalSection(&ScLock);
    if (Card->ShareMode != SCARD_SHARE_DIRECT)
        Result = ScApplyDisposition(Reader, Disposition);
    EnterCriticalSection(&ScLock);
    if (Reader->Transaction == Card)
        Reader->Transaction = NULL;
    WakeAllConditionVariable(&ScChanged);
    LeaveCriticalSection(&ScLock);
    ScDereferenceReader(Reader);
    return Result;
}

LONG
ScStatus(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD NamesSize,
    BYTE *Names,
    DWORD *NamesReturned,
    DWORD *State,
    DWORD *Protocol,
    BYTE Atr[36],
    DWORD *AtrLength)
{
    SC_CARD *Card;
    SC_READER *Reader;
    DWORD Length;
    LONG Result;

    *NamesReturned = *State = *Protocol = *AtrLength = 0;
    EnterCriticalSection(&ScLock);
    Card = ScFindCard(Session, Id);
    Result = Card ? ScCheckCard(Card) : ERROR_INVALID_HANDLE;
    if (Result == SCARD_S_SUCCESS)
    {
        Reader = Card->Reader;
        Length = (DWORD)strlen(Reader->Name) + 2;
        if (Length > NamesSize)
        {
            Result = SCARD_E_INSUFFICIENT_BUFFER;
        }
        else
        {
            memcpy(Names, Reader->Name, Length - 1);
            Names[Length - 1] = 0;
            *NamesReturned = Length;
            *State = Reader->State <= SCARD_ABSENT ? SCARD_ABSENT : Reader->State;
            *Protocol = Card->Protocol;
            if (Reader->State > SCARD_ABSENT)
            {
                *AtrLength = Reader->AtrLength;
                memcpy(Atr, Reader->Atr, Reader->AtrLength);
            }
        }
    }
    LeaveCriticalSection(&ScLock);
    return Result;
}

static
LONG
ScWaitTransaction(
    _In_ SC_SESSION *Session,
    _In_ ULONGLONG Id)
{
    SC_CARD *Card;
    LONG Result;

    EnterCriticalSection(&ScLock);
    for (;;)
    {
        Card = ScFindCard(Session, Id);
        if (!Card)
        {
            Result = ERROR_INVALID_HANDLE;
            break;
        }
        Result = ScCheckCard(Card);
        if (Result != SCARD_S_SUCCESS || !Card->Reader->Transaction || Card->Reader->Transaction == Card)
            break;
        SleepConditionVariableCS(&ScChanged, &ScLock, INFINITE);
    }
    if (Result == SCARD_S_SUCCESS)
        ScReferenceReader(Card->Reader);
    LeaveCriticalSection(&ScLock);
    return Result;
}

LONG
ScTransmit(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD SendProtocol,
    DWORD SendPciLength,
    DWORD SendLength,
    BYTE *Send,
    DWORD ReceiveSize,
    BYTE *Receive,
    DWORD *Received,
    DWORD *ReceiveProtocol,
    DWORD *ReceivePciLength)
{
    SCARD_IO_REQUEST *Request, *Reply;
    SC_CARD *Card;
    SC_READER *Reader;
    DWORD Returned = 0, Error;
    LONG Result;

    UNREFERENCED_PARAMETER(SendPciLength);
    *Received = *ReceiveProtocol = *ReceivePciLength = 0;
    Result = ScWaitTransaction(Session, Id);
    if (Result != SCARD_S_SUCCESS)
        return Result;
    EnterCriticalSection(&ScLock);
    Card = ScFindCard(Session, Id);
    Reader = Card ? Card->Reader : NULL;
    if (!Card)
        Result = ERROR_INVALID_HANDLE;
    else if (Card->ShareMode == SCARD_SHARE_DIRECT || !Card->Protocol)
        Result = SCARD_E_NOT_READY;
    else if (SendProtocol != Card->Protocol)
        Result = SCARD_E_PROTO_MISMATCH;
    LeaveCriticalSection(&ScLock);
    if (Result != SCARD_S_SUCCESS)
    {
        if (Reader)
            ScDereferenceReader(Reader);
        return Result;
    }

    Request = HeapAlloc(GetProcessHeap(), 0, sizeof(*Request) + SendLength);
    Reply = HeapAlloc(GetProcessHeap(), 0, sizeof(*Reply) + ReceiveSize);
    if (!Request || !Reply)
    {
        Result = SCARD_E_NO_MEMORY;
    }
    else
    {
        Request->dwProtocol = SendProtocol;
        Request->cbPciLength = sizeof(*Request);
        memcpy(Request + 1, Send, SendLength);
        EnterCriticalSection(&Reader->IoLock);
        Error = ScIoctl(Reader, IOCTL_SMARTCARD_TRANSMIT, Request, sizeof(*Request) + SendLength, Reply,
                        sizeof(*Reply) + ReceiveSize, &Returned);
        LeaveCriticalSection(&Reader->IoLock);
        Result = ScMapError(Error);
        if (Result == SCARD_S_SUCCESS && Returned < sizeof(*Reply))
            Result = SCARD_F_COMM_ERROR;
        if (Result == SCARD_S_SUCCESS)
        {
            *Received = Returned - sizeof(*Reply);
            *ReceiveProtocol = Reply->dwProtocol;
            *ReceivePciLength = Reply->cbPciLength;
            memcpy(Receive, Reply + 1, *Received);
        }
    }
    if (Request)
        HeapFree(GetProcessHeap(), 0, Request);
    if (Reply)
        HeapFree(GetProcessHeap(), 0, Reply);
    ScDereferenceReader(Reader);
    return Result;
}

LONG
ScControl(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD Code,
    DWORD InLength,
    BYTE *In,
    DWORD OutSize,
    BYTE *Out,
    DWORD *Returned)
{
    SC_CARD *Card;
    SC_READER *Reader;
    DWORD Error;
    LONG Result;

    *Returned = 0;
    Result = ScAcquireCard(Session, Id, &Card, TRUE);
    if (Result != SCARD_S_SUCCESS)
        return Result;
    Reader = Card->Reader;
    EnterCriticalSection(&Reader->IoLock);
    Error = ScIoctl(Reader, Code, In, InLength, Out, OutSize, Returned);
    LeaveCriticalSection(&Reader->IoLock);
    ScDereferenceReader(Reader);
    return Error == ERROR_SUCCESS ? SCARD_S_SUCCESS : (LONG)Error;
}

static
LONG
ScNameAttribute(
    _In_ SC_READER *Reader,
    _In_ BOOL Wide,
    _In_ DWORD Size,
    _Out_writes_bytes_to_(Size, *Returned) BYTE *Attribute,
    _Out_ DWORD *Returned,
    _Out_ DWORD *Length)
{
    DWORD Chars = (DWORD)strlen(Reader->Name) + 1, Index;

    *Length = Wide ? Chars * sizeof(WCHAR) : Chars;
    if (*Length > Size)
        return SCARD_S_SUCCESS;
    for (Index = 0; Index < Chars; ++Index)
    {
        if (Wide)
            ((WCHAR *)Attribute)[Index] = (UCHAR)Reader->Name[Index];
        else
            Attribute[Index] = Reader->Name[Index];
    }
    *Returned = *Length;
    return SCARD_S_SUCCESS;
}

LONG
ScGetAttrib(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD AttributeId,
    DWORD Size,
    BYTE *Attribute,
    DWORD *Returned,
    DWORD *Length)
{
    SC_CARD *Card;
    SC_READER *Reader;
    DWORD Error;
    LONG Result;

    *Returned = *Length = 0;
    Result = ScAcquireCard(Session, Id, &Card, TRUE);
    if (Result != SCARD_S_SUCCESS)
        return Result;
    Reader = Card->Reader;
    switch (AttributeId)
    {
        case SCARD_ATTR_DEVICE_FRIENDLY_NAME_A:
        case SCARD_ATTR_DEVICE_SYSTEM_NAME_A:
            Result = ScNameAttribute(Reader, FALSE, Size, Attribute, Returned, Length);
            break;

        case SCARD_ATTR_DEVICE_FRIENDLY_NAME_W:
        case SCARD_ATTR_DEVICE_SYSTEM_NAME_W:
            Result = ScNameAttribute(Reader, TRUE, Size, Attribute, Returned, Length);
            break;

        default:
            EnterCriticalSection(&Reader->IoLock);
            Error = ScIoctl(Reader, IOCTL_SMARTCARD_GET_ATTRIBUTE, &AttributeId, sizeof(AttributeId), Attribute, Size,
                            Returned);
            LeaveCriticalSection(&Reader->IoLock);
            Result = Error == ERROR_SUCCESS ? SCARD_S_SUCCESS : (LONG)Error;
            *Length = *Returned;
            break;
    }
    ScDereferenceReader(Reader);
    return Result;
}

LONG
ScSetAttrib(
    SCARDSVR_SESSION Session,
    ULONGLONG Id,
    DWORD AttributeId,
    DWORD Length,
    BYTE *Attribute)
{
    SC_CARD *Card;
    SC_READER *Reader;
    BYTE *Request;
    DWORD Returned, Error;
    LONG Result;

    Result = ScAcquireCard(Session, Id, &Card, TRUE);
    if (Result != SCARD_S_SUCCESS)
        return Result;
    Reader = Card->Reader;
    Request = HeapAlloc(GetProcessHeap(), 0, sizeof(DWORD) + Length);
    if (!Request)
    {
        ScDereferenceReader(Reader);
        return SCARD_E_NO_MEMORY;
    }
    memcpy(Request, &AttributeId, sizeof(DWORD));
    memcpy(Request + sizeof(DWORD), Attribute, Length);
    EnterCriticalSection(&Reader->IoLock);
    Error = ScIoctl(Reader, IOCTL_SMARTCARD_SET_ATTRIBUTE, Request, sizeof(DWORD) + Length, NULL, 0, &Returned);
    LeaveCriticalSection(&Reader->IoLock);
    HeapFree(GetProcessHeap(), 0, Request);
    ScDereferenceReader(Reader);
    return Error == ERROR_SUCCESS ? SCARD_S_SUCCESS : (LONG)Error;
}
