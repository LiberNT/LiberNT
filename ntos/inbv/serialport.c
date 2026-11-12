/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The serial lines the boot display falls back on when there is no screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* One line the headless terminal may be speaking over */
typedef struct _INBV_PORT
{
    CPPORT Port;
    BOOLEAN InUse;
    BOOLEAN IsMMIODevice;
} INBV_PORT, *PINBV_PORT;

#define INBV_PORT_COUNT (MAX_COM_PORTS + 1)

static INBV_PORT InbvPorts[INBV_PORT_COUNT];

/* PRIVATE FUNCTIONS **********************************************************/

/* The line an identifier names, or nothing when it names none */
static
PINBV_PORT
InbvPortFromId(
    _In_ ULONG PortId)
{
    if (PortId == 0 || PortId >= INBV_PORT_COUNT) return NULL;
    if (!InbvPorts[PortId].InUse) return NULL;

    return &InbvPorts[PortId];
}

static
PUCHAR
InbvPortDefaultAddress(
    _In_ ULONG PortNumber)
{
    if (PortNumber == 0 || PortNumber > MAX_COM_PORTS) return NULL;
    if (BaseArray[PortNumber] == 0) return NULL;

    return (PUCHAR)(ULONG_PTR)BaseArray[PortNumber];
}

/* The first free slot, preferring the one the caller's line number asks for */
static
ULONG
InbvPortClaimSlot(
    _In_ ULONG PortNumber)
{
    ULONG Slot;

    if (PortNumber != 0 && PortNumber < INBV_PORT_COUNT &&
        !InbvPorts[PortNumber].InUse)
    {
        return PortNumber;
    }

    for (Slot = 1; Slot < INBV_PORT_COUNT; Slot++)
    {
        if (!InbvPorts[Slot].InUse) return Slot;
    }

    return 0;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
InbvPortInitialize(
    _In_ ULONG BaudRate,
    _In_ ULONG PortNumber,
    _In_ PUCHAR PortAddress,
    _Out_ PULONG PortId,
    _In_ BOOLEAN IsMMIODevice)
{
    PINBV_PORT Entry;
    NTSTATUS Status;
    ULONG Slot;

    if (PortId == NULL) return FALSE;

    *PortId = 0;

    if (PortAddress == NULL)
    {
        PortAddress = InbvPortDefaultAddress(PortNumber);
        if (PortAddress == NULL) return FALSE;
    }

    if (BaudRate == 0) BaudRate = DEFAULT_BAUD_RATE;

    Slot = InbvPortClaimSlot(PortNumber);
    if (Slot == 0) return FALSE;

    Entry = &InbvPorts[Slot];

    /* A line nobody answers on is worse than no line at all */
    if (!IsMMIODevice && !CpDoesPortExist(PortAddress)) return FALSE;

    Status = CpInitialize(&Entry->Port, PortAddress, BaudRate);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* TODO: drive a memory-mapped register block through the register accessors */
    Entry->IsMMIODevice = IsMMIODevice;
    Entry->InUse = TRUE;

    *PortId = Slot;
    return TRUE;
}

/* Gives a line back */
VOID
NTAPI
InbvPortTerminate(
    _In_ ULONG PortId)
{
    PINBV_PORT Entry = InbvPortFromId(PortId);

    if (Entry == NULL) return;

    Entry->InUse = FALSE;
    Entry->IsMMIODevice = FALSE;
    Entry->Port.Address = NULL;
    Entry->Port.BaudRate = 0;
    Entry->Port.Flags = 0;
}

/* Turns the line's hardware queues on or off */
VOID
NTAPI
InbvPortEnableFifo(
    _In_ ULONG PortId,
    _In_ BOOLEAN Enable)
{
    PINBV_PORT Entry = InbvPortFromId(PortId);

    if (Entry == NULL || Entry->Port.Address == NULL) return;

    CpEnableFifo(Entry->Port.Address, Enable);
}

/* Puts one byte on the line, waiting for room if there is none */
VOID
NTAPI
InbvPortPutByte(
    _In_ ULONG PortId,
    _In_ UCHAR Byte)
{
    PINBV_PORT Entry = InbvPortFromId(PortId);

    if (Entry == NULL) return;

    CpPutByte(&Entry->Port, Byte);
}

BOOLEAN
NTAPI
InbvPortGetByte(
    _In_ ULONG PortId,
    _Out_ PUCHAR Byte)
{
    PINBV_PORT Entry = InbvPortFromId(PortId);

    if (Byte == NULL) return FALSE;

    *Byte = 0;

    if (Entry == NULL) return FALSE;

    return CpGetByte(&Entry->Port, Byte, FALSE, FALSE) == CP_GET_SUCCESS;
}

BOOLEAN
NTAPI
InbvPortPollOnly(
    _In_ ULONG PortId)
{
    PINBV_PORT Entry = InbvPortFromId(PortId);
    UCHAR Byte;

    if (Entry == NULL) return FALSE;

    return CpGetByte(&Entry->Port, &Byte, FALSE, TRUE) == CP_GET_SUCCESS;
}

/* EOF */
