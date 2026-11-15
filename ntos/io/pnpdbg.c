/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Printing what the PnP manager is holding, for a human to read
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "ioint.h"
#include "pnpio.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

PCM_PARTIAL_RESOURCE_DESCRIPTOR
NTAPI
PipGetNextCmPartialDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Next = CmDescriptor + 1;

    if (CmDescriptor->Type != CmResourceTypeDeviceSpecific) return Next;

    return (PCM_PARTIAL_RESOURCE_DESCRIPTOR)
        ((PUCHAR)Next + CmDescriptor->u.DeviceSpecificData.DataSize);
}

VOID
NTAPI
PipDumpCmResourceDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel)
{
    UNREFERENCED_PARAMETER(DebugLevel);

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
            DPRINT1("  port %I64x for %lx, share %x, flags %x\n",
                    Descriptor->u.Port.Start.QuadPart,
                    Descriptor->u.Port.Length,
                    Descriptor->ShareDisposition,
                    Descriptor->Flags);
            break;

        case CmResourceTypeInterrupt:
            DPRINT1("  interrupt level %lx vector %lx affinity %p\n",
                    Descriptor->u.Interrupt.Level,
                    Descriptor->u.Interrupt.Vector,
                    (PVOID)Descriptor->u.Interrupt.Affinity);
            break;

        case CmResourceTypeMemory:
            DPRINT1("  memory %I64x for %lx, share %x, flags %x\n",
                    Descriptor->u.Memory.Start.QuadPart,
                    Descriptor->u.Memory.Length,
                    Descriptor->ShareDisposition,
                    Descriptor->Flags);
            break;

        case CmResourceTypeDma:
            DPRINT1("  dma channel %lx port %lx\n",
                    Descriptor->u.Dma.Channel,
                    Descriptor->u.Dma.Port);
            break;

        case CmResourceTypeDeviceSpecific:
            DPRINT1("  device specific, %lx bytes\n",
                    Descriptor->u.DeviceSpecificData.DataSize);
            break;

        case CmResourceTypeBusNumber:
            DPRINT1("  bus numbers %lx for %lx\n",
                    Descriptor->u.BusNumber.Start,
                    Descriptor->u.BusNumber.Length);
            break;

        case CmResourceTypeDevicePrivate:
            DPRINT1("  device private %lx %lx %lx\n",
                    Descriptor->u.DevicePrivate.Data[0],
                    Descriptor->u.DevicePrivate.Data[1],
                    Descriptor->u.DevicePrivate.Data[2]);
            break;

        case CmResourceTypeConnection:
            DPRINT1("  connection class %x type %x id %lx:%lx\n",
                    Descriptor->u.Connection.Class,
                    Descriptor->u.Connection.Type,
                    Descriptor->u.Connection.IdHighPart,
                    Descriptor->u.Connection.IdLowPart);
            break;

        default:
            DPRINT1("  resource type %x\n", Descriptor->Type);
            break;
    }
}
