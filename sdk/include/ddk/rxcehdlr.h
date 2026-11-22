/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RDBSS connection engine event handler definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RXCEHDLR_
#define _RXCEHDLR_

#include "tdi.h"

typedef TDI_CONNECTION_INFORMATION RXCE_CONNECTION_INFORMATION;

typedef RXCE_CONNECTION_INFORMATION *PRXCE_CONNECTION_INFORMATION;

typedef NTSTATUS (*PRXCE_IND_DATA_READY)(PVOID, PMDL, ULONG, NTSTATUS);

typedef NTSTATUS (*PRXCE_IND_RECEIVE_DATAGRAM)(PVOID, int, PVOID, int, PVOID, ULONG, ULONG, ULONG, ULONG *, PVOID, PMDL *, PULONG);

/* TODO: the remaining connection engine handler types and RXCE_HANDLER */

#endif /* _RXCEHDLR_ */
