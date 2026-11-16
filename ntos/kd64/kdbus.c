/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debugger reads and writes of bus configuration data
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Stefan Ginsberg <stefan.ginsberg@reactos.org>
 *              Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

ULONG
NTAPI
HalpKdReadPciConfig(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length);

NTSTATUS
NTAPI
KdpSysReadBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    ULONG Read;

    /* A processor frozen by NMI may hold the PCI configuration lock, so a stopped machine never waits on it */
    if (KdEnteredDebugger && (BusDataType == PCIConfiguration))
    {
        Read = HalpKdReadPciConfig(BusNumber, SlotNumber, Buffer, Offset, Length);
        if (Read == MAXULONG)
        {
            *ActualLength = 0;
            return STATUS_DEVICE_BUSY;
        }
    }
    else
    {
        Read = HalGetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, Offset, Length);
    }

    *ActualLength = Read;
    return (Read != 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
KdpSysWriteBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    /* FIXME: on a stopped machine this waits for a PCI configuration lock a frozen processor may hold */
    *ActualLength = HalSetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, Offset, Length);
    return (*ActualLength != 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* EOF */
