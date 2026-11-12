/*
 * PROJECT:     LiberNT Smart Card Service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Smart card resource manager definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winsvc.h>
#include <winuser.h>
#include <dbt.h>
#include <setupapi.h>
#include <winscard.h>
#include <winsmcrd.h>
#include <rpc.h>
#include <strsafe.h>
#include <ndk/rtlfuncs.h>

#include "scardsvr_s.h"

#define SC_MAX_NAME 160

typedef struct _SC_CARD SC_CARD;

typedef struct _SC_READER
{
    LIST_ENTRY Entry;
    LONG References;
    WCHAR Path[MAX_PATH];
    CHAR Name[SC_MAX_NAME];
    HANDLE Device;
    CRITICAL_SECTION IoLock;
    BOOL Listed;
    BOOL Removed;
    BOOL Mute;
    DWORD State;
    BYTE Atr[SCARDSVR_MAX_ATR];
    DWORD AtrLength;
    DWORD Events;
    DWORD Resets;
    DWORD Protocol;
    ULONG Shared;
    SC_CARD *Exclusive;
    SC_CARD *Transaction;
} SC_READER;

typedef struct _SC_SESSION
{
    LIST_ENTRY Contexts;
} SC_SESSION;

typedef struct _SC_CONTEXT
{
    LIST_ENTRY Entry;
    ULONGLONG Id;
    SC_SESSION *Session;
    LONG References;
    LONG Cancels;
    BOOL Released;
    LIST_ENTRY Cards;
} SC_CONTEXT;

struct _SC_CARD
{
    LIST_ENTRY Entry;
    ULONGLONG Id;
    SC_CONTEXT *Context;
    SC_READER *Reader;
    DWORD ShareMode;
    DWORD Protocol;
    DWORD Events;
    DWORD Resets;
};

extern CRITICAL_SECTION ScLock;
extern CONDITION_VARIABLE ScChanged;
extern LIST_ENTRY ScReaders;

VOID
ScAddReader(
    _In_ PCWSTR Path);

VOID
ScRemoveReader(
    _In_ PCWSTR Path);

VOID
ScReferenceReader(
    _Inout_ SC_READER *Reader);

VOID
ScDereferenceReader(
    _Inout_ SC_READER *Reader);

DWORD
ScIoctl(
    _In_ SC_READER *Reader,
    _In_ DWORD Code,
    _In_reads_bytes_opt_(InLength) PVOID In,
    _In_ DWORD InLength,
    _Out_writes_bytes_opt_(OutLength) PVOID Out,
    _In_ DWORD OutLength,
    _Out_ PDWORD Returned);

LONG
ScPowerCard(
    _Inout_ SC_READER *Reader,
    _In_ DWORD Disposition);

LONG
ScSetProtocol(
    _Inout_ SC_READER *Reader,
    _In_ DWORD Preferred,
    _Out_ PDWORD Protocol);

LONG
ScMapError(
    _In_ DWORD Error);

DWORD
ScStartRpcServer(VOID);
