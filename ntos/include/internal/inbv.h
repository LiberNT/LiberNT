/*
 *              Copyright 2019-2022 Hermès Bélusca-Maïto
 */

#pragma once
#include "bootvid/bootvid.h"

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvDriverInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count
);

extern BOOLEAN InbvBootDriverInstalled;

BOOLEAN
NTAPI

VOID
NTAPI

VOID
NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

NTAPI

PUCHAR
NTAPI
InbvGetResourceAddress(

BOOLEAN
NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI
InbvIndicateProgress(VOID);

VOID
NTAPI
InbvPortEnableFifo(
    _In_ ULONG PortId,
    _In_ BOOLEAN Enable);

BOOLEAN
NTAPI
InbvPortGetByte(
    _In_ ULONG PortId,
    _Out_ PUCHAR Byte);

BOOLEAN
NTAPI
InbvPortInitialize(
    _In_ ULONG BaudRate,
    _In_ ULONG PortNumber,
    _In_ PUCHAR PortAddress,
    _Out_ PULONG PortId,
    _In_ BOOLEAN IsMMIODevice);

VOID
NTAPI
InbvPortTerminate(
    _In_ ULONG PortId);
