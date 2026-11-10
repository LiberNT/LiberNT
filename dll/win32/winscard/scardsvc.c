/*
 * PROJECT:     LiberNT Smart Card API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Smart card calls served by the smart card resource manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winscard.h"
#include "rpc.h"
#include "scardsvr_c.h"
#include "unixlib.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winscard);

static SRWLOCK SessionLock = SRWLOCK_INIT;
static SCARDSVR_SESSION Session;

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

static
LONG
ScardMapRpcError(
    _In_ ULONG Code)
{
    switch (Code)
    {
        case RPC_S_SERVER_UNAVAILABLE:
        case RPC_S_CALL_FAILED:
        case RPC_S_CALL_FAILED_DNE:
        case RPC_X_SS_IN_NULL_CONTEXT:
        case RPC_X_SS_CONTEXT_MISMATCH:
            return SCARD_E_NO_SERVICE;

        case RPC_S_OUT_OF_MEMORY:
            return SCARD_E_NO_MEMORY;

        default:
            return SCARD_F_COMM_ERROR;
    }
}

static
SCARDSVR_SESSION
ScardOpenSession(VOID)
{
    SCARDSVR_SESSION Current;
    RPC_WSTR StringBinding = NULL;
    handle_t Binding = NULL;
    LONG Result = SCARD_E_NO_SERVICE;

    AcquireSRWLockShared(&SessionLock);
    Current = Session;
    ReleaseSRWLockShared(&SessionLock);
    if (Current)
        return Current;

    AcquireSRWLockExclusive(&SessionLock);
    if (!Session &&
        RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL, (RPC_WSTR)SCARDSVR_ENDPOINT, NULL,
                                 &StringBinding) == RPC_S_OK &&
        RpcBindingFromStringBindingW(StringBinding, &Binding) == RPC_S_OK)
    {
        RpcTryExcept
        {
            Result = ScOpenSession(Binding, &Session);
        }
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)
        {
            Result = ScardMapRpcError(RpcExceptionCode());
        }
        RpcEndExcept;
        if (Result != SCARD_S_SUCCESS)
            Session = NULL;
    }
    if (StringBinding)
        RpcStringFreeW(&StringBinding);
    if (Binding)
        RpcBindingFree(&Binding);
    Current = Session;
    ReleaseSRWLockExclusive(&SessionLock);
    if (!Current)
        WARN("the smart card service is not reachable: %#lx\n", Result);
    return Current;
}

static
LONG
ScardRpcFailed(
    _In_ SCARDSVR_SESSION Failed,
    _In_ ULONG Code)
{
    LONG Result = ScardMapRpcError(Code);

    if (Result == SCARD_E_NO_SERVICE)
    {
        AcquireSRWLockExclusive(&SessionLock);
        if (Session == Failed)
        {
            RpcSsDestroyClientContext(&Session);
            Session = NULL;
        }
        ReleaseSRWLockExclusive(&SessionLock);
    }
    return Result;
}

#define SCARD_RPC(Call)                                         \
    do                                                          \
    {                                                           \
        SCARDSVR_SESSION Current = ScardOpenSession();          \
        if (!Current)                                           \
            return SCARD_E_NO_SERVICE;                          \
        RpcTryExcept                                            \
        {                                                       \
            Result = Call;                                      \
        }                                                       \
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)                    \
        {                                                       \
            Result = ScardRpcFailed(Current, RpcExceptionCode()); \
        }                                                       \
        RpcEndExcept;                                           \
    } while (0)

static
LONG
ScardEstablishContext(
    _Inout_ struct scard_establish_context_params *Params)
{
    ULONGLONG Context = 0;
    LONG Result;

    SCARD_RPC(ScEstablishContext(Current, (DWORD)Params->scope, &Context));
    if (Result == SCARD_S_SUCCESS)
        *Params->handle = Context;
    return Result;
}

static
LONG
ScardGetStatusChange(
    _Inout_ struct scard_get_status_change_params *Params)
{
    SCARDSVR_READER_IN *In;
    SCARDSVR_READER_OUT *Out;
    ULONG Index;
    LONG Result;

    if (Params->count > 256)
        return SCARD_E_INVALID_VALUE;
    In = calloc(Params->count ? Params->count : 1, sizeof(*In));
    Out = calloc(Params->count ? Params->count : 1, sizeof(*Out));
    if (!In || !Out)
    {
        free(In);
        free(Out);
        return SCARD_E_NO_MEMORY;
    }
    for (Index = 0; Index < Params->count; ++Index)
    {
        In[Index].Reader = Params->states[Index].reader ? (char *)(ULONG_PTR)Params->states[Index].reader : "";
        In[Index].CurrentState = (DWORD)Params->states[Index].current_state;
    }
    SCARD_RPC(ScGetStatusChange(Current, Params->handle, (DWORD)Params->timeout, (DWORD)Params->count, In, Out));
    if (Result == SCARD_S_SUCCESS)
    {
        for (Index = 0; Index < Params->count; ++Index)
        {
            Params->states[Index].event_state = Out[Index].EventState;
            Params->states[Index].atr_size = min(Out[Index].AtrLength, MAX_ATR_SIZE);
            memcpy(Params->states[Index].atr, Out[Index].Atr, Params->states[Index].atr_size);
        }
    }
    free(In);
    free(Out);
    return Result;
}

static
LONG
ScardCopyOut(
    _In_reads_bytes_(Length) const BYTE *Source,
    _In_ DWORD Length,
    _Out_writes_bytes_opt_(*Size) void *Destination,
    _Inout_ UINT64 *Size)
{
    if (Destination && *Size < Length)
    {
        *Size = Length;
        return SCARD_E_INSUFFICIENT_BUFFER;
    }
    if (Destination)
        memcpy(Destination, Source, Length);
    *Size = Length;
    return SCARD_S_SUCCESS;
}

static
LONG
ScardListReaders(
    _Inout_ struct scard_list_readers_params *Params)
{
    DWORD Returned = 0;
    BYTE *Buffer;
    LONG Result;

    Buffer = malloc(SCARDSVR_MAX_NAMES);
    if (!Buffer)
        return SCARD_E_NO_MEMORY;
    SCARD_RPC(ScListReaders(Current, Params->handle, (char *)Params->groups, SCARDSVR_MAX_NAMES, Buffer, &Returned));
    if (Result == SCARD_S_SUCCESS)
        Result = ScardCopyOut(Buffer, Returned, Params->readers, Params->readers_len);
    free(Buffer);
    return Result;
}

static
LONG
ScardListReaderGroups(
    _Inout_ struct scard_list_reader_groups_params *Params)
{
    DWORD Returned = 0;
    BYTE *Buffer;
    LONG Result;

    Buffer = malloc(SCARDSVR_MAX_NAMES);
    if (!Buffer)
        return SCARD_E_NO_MEMORY;
    SCARD_RPC(ScListReaderGroups(Current, Params->handle, SCARDSVR_MAX_NAMES, Buffer, &Returned));
    if (Result == SCARD_S_SUCCESS)
        Result = ScardCopyOut(Buffer, Returned, Params->groups, Params->groups_len);
    free(Buffer);
    return Result;
}

static
LONG
ScardConnect(
    _Inout_ struct scard_connect_params *Params)
{
    ULONGLONG Card = 0;
    DWORD Protocol = 0;
    LONG Result;

    if (!Params->reader)
        return SCARD_E_INVALID_PARAMETER;
    SCARD_RPC(ScConnect(Current, Params->context_handle, (char *)Params->reader, (DWORD)Params->share_mode,
                        (DWORD)Params->preferred_protocols, &Card, &Protocol));
    if (Result == SCARD_S_SUCCESS)
    {
        *Params->connect_handle = Card;
        *Params->protocol = Protocol;
    }
    return Result;
}

static
LONG
ScardStatus(
    _Inout_ struct scard_status_params *Params)
{
    DWORD Returned = 0, State = 0, Protocol = 0, AtrLength = 0;
    BYTE Atr[SCARDSVR_MAX_ATR];
    BYTE *Names;
    LONG Result;

    Names = malloc(SCARDSVR_MAX_NAMES);
    if (!Names)
        return SCARD_E_NO_MEMORY;
    SCARD_RPC(ScStatus(Current, Params->handle, SCARDSVR_MAX_NAMES, Names, &Returned, &State, &Protocol, Atr,
                       &AtrLength));
    if (Result == SCARD_S_SUCCESS)
        Result = ScardCopyOut(Names, Returned, Params->names, Params->names_len);
    if (Result == SCARD_S_SUCCESS && Params->atr_len)
        Result = ScardCopyOut(Atr, min(AtrLength, sizeof(Atr)), Params->atr, Params->atr_len);
    if (Result == SCARD_S_SUCCESS)
    {
        *Params->state = State;
        *Params->protocol = Protocol;
    }
    free(Names);
    return Result;
}

static
LONG
ScardReconnect(
    _Inout_ struct scard_reconnect_params *Params)
{
    DWORD Protocol = 0;
    LONG Result;

    SCARD_RPC(ScReconnect(Current, Params->handle, (DWORD)Params->share_mode, (DWORD)Params->preferred_protocols,
                          (DWORD)Params->initialization, &Protocol));
    if (Result == SCARD_S_SUCCESS)
        *Params->protocol = Protocol;
    return Result;
}

static
LONG
ScardTransmit(
    _Inout_ struct scard_transmit_params *Params)
{
    DWORD Received = 0, Protocol = 0, PciLength = 0;
    DWORD Size = (DWORD)min(*Params->recv_buflen, SCARDSVR_MAX_BUFFER);
    BYTE Empty = 0;
    LONG Result;

    if (Params->send_buflen > SCARDSVR_MAX_BUFFER || (!Params->send_buf && Params->send_buflen))
        return SCARD_E_INVALID_PARAMETER;
    if (!Params->recv_buf)
        Size = 0;
    SCARD_RPC(ScTransmit(Current, Params->handle, (DWORD)Params->send->protocol, (DWORD)Params->send->pci_len,
                         (DWORD)Params->send_buflen, Params->send_buf ? (BYTE *)Params->send_buf : &Empty, Size,
                         Params->recv_buf ? Params->recv_buf : &Empty, &Received, &Protocol, &PciLength));
    if (Result == SCARD_S_SUCCESS)
    {
        Params->recv->protocol = Protocol;
        Params->recv->pci_len = PciLength;
        *Params->recv_buflen = Received;
    }
    return Result;
}

static
LONG
ScardControl(
    _Inout_ struct scard_control_params *Params)
{
    DWORD Returned = 0, Size = (DWORD)min(Params->out_buflen, SCARDSVR_MAX_BUFFER);
    BYTE Empty = 0;
    LONG Result;

    if (Params->in_buflen > SCARDSVR_MAX_BUFFER)
        return SCARD_E_INVALID_PARAMETER;
    if (!Params->out_buf)
        Size = 0;
    SCARD_RPC(ScControl(Current, Params->handle, (DWORD)Params->code, Params->in_buf ? (DWORD)Params->in_buflen : 0,
                        (BYTE *)Params->in_buf, Size, Params->out_buf ? Params->out_buf : &Empty, &Returned));
    if (Result == SCARD_S_SUCCESS)
        *Params->ret_len = Returned;
    return Result;
}

static
LONG
ScardGetAttrib(
    _Inout_ struct scard_get_attrib_params *Params)
{
    BOOL Allocate = *Params->attr_len == SCARD_AUTOALLOCATE;
    DWORD Returned = 0, Length = 0;
    BYTE *Buffer;
    LONG Result;

    Buffer = malloc(SCARDSVR_MAX_NAMES);
    if (!Buffer)
        return SCARD_E_NO_MEMORY;
    SCARD_RPC(ScGetAttrib(Current, Params->handle, (DWORD)Params->id, SCARDSVR_MAX_NAMES, Buffer, &Returned, &Length));
    if (Result == SCARD_S_SUCCESS && Returned != Length)
        Result = SCARD_E_INSUFFICIENT_BUFFER;
    if (Result == SCARD_S_SUCCESS && Allocate && Params->attr)
    {
        *(BYTE **)Params->attr = Buffer;
        *Params->attr_len = Length;
        return SCARD_S_SUCCESS;
    }
    if (Result == SCARD_S_SUCCESS)
        Result = ScardCopyOut(Buffer, Length, Params->attr, Params->attr_len);
    free(Buffer);
    return Result;
}

LONG
ScardBackendCall(
    _In_ ULONG Function,
    _Inout_ void *Params)
{
    LONG Result;

    switch (Function)
    {
        case unix_scard_establish_context:
            return ScardEstablishContext(Params);

        case unix_scard_release_context:
            SCARD_RPC(ScReleaseContext(Current, ((struct scard_release_context_params *)Params)->handle));
            return Result;

        case unix_scard_is_valid_context:
            SCARD_RPC(ScIsValidContext(Current, ((struct scard_is_valid_context_params *)Params)->handle));
            return Result;

        case unix_scard_get_status_change:
            return ScardGetStatusChange(Params);

        case unix_scard_cancel:
            SCARD_RPC(ScCancel(Current, ((struct scard_cancel_params *)Params)->handle));
            return Result;

        case unix_scard_list_readers:
            return ScardListReaders(Params);

        case unix_scard_list_reader_groups:
            return ScardListReaderGroups(Params);

        case unix_scard_connect:
            return ScardConnect(Params);

        case unix_scard_status:
            return ScardStatus(Params);

        case unix_scard_reconnect:
            return ScardReconnect(Params);

        case unix_scard_disconnect:
        {
            struct scard_disconnect_params *Disconnect = Params;

            SCARD_RPC(ScDisconnect(Current, Disconnect->handle, (DWORD)Disconnect->disposition));
            return Result;
        }

        case unix_scard_begin_transaction:
            SCARD_RPC(ScBeginTransaction(Current, ((struct scard_begin_transaction_params *)Params)->handle));
            return Result;

        case unix_scard_end_transaction:
        {
            struct scard_end_transaction_params *End = Params;

            SCARD_RPC(ScEndTransaction(Current, End->handle, (DWORD)End->disposition));
            return Result;
        }

        case unix_scard_transmit:
            return ScardTransmit(Params);

        case unix_scard_control:
            return ScardControl(Params);

        case unix_scard_get_attrib:
            return ScardGetAttrib(Params);

        case unix_scard_set_attrib:
        {
            struct scard_set_attrib_params *Set = Params;
            BYTE Empty = 0;

            if (Set->attr_len > SCARDSVR_MAX_NAMES || (!Set->attr && Set->attr_len))
                return SCARD_E_INVALID_PARAMETER;
            SCARD_RPC(ScSetAttrib(Current, Set->handle, (DWORD)Set->id, (DWORD)Set->attr_len,
                                  Set->attr ? (BYTE *)Set->attr : &Empty));
            return Result;
        }

        default:
            return SCARD_F_INTERNAL_ERROR;
    }
}
