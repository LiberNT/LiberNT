/*
 * PROJECT:     LiberNT PowerVR Rogue WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter context and platform interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <dispmprt.h>
#include <libernt/powervr_umd.h>

#include "glue/powervr_glue.h"

#define POWERVR_TAG             'rvwP'
#define POWERVR_ADAPTER_MAGIC   0x52564150UL
#define POWERVR_DEVICE_MAGIC    0x52564450UL

#define POWERVR_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_0
#define POWERVR_DECLARED_WDDM_VERSION      DXGKDDI_WDDMv2

#define POWERVR_SEGMENT_SIZE    (1024 * 1024)
#define POWERVR_DMA_LIMIT       0x7FFFFFFFULL

#define ROGUE_CR_CORE_ID        0x0018U
#define ROGUE_CR_CORE_ID__PBVNC 0x0020U

typedef struct _POWERVR_ADAPTER POWERVR_ADAPTER, *PPOWERVR_ADAPTER;

typedef struct _POWERVR_PLATFORM
{
    PCWSTR HardwareId;
    PCSTR Name;
    ULONG64 CoreClockHz;
    const char *const *Compatible;
    ULONG CompatibleCount;
    NTSTATUS (*PowerUp)(_Inout_ PPOWERVR_ADAPTER Adapter);
    VOID (*PowerDown)(_Inout_ PPOWERVR_ADAPTER Adapter);
} POWERVR_PLATFORM, *PPOWERVR_PLATFORM;

struct _POWERVR_ADAPTER
{
    ULONG Magic;
    PDEVICE_OBJECT PhysicalDeviceObject;
    DXGKRNL_INTERFACE Dxgk;
    DXGK_DEVICE_INFO DeviceInfo;
    PHYSICAL_ADDRESS RegistersPhysical;
    ULONG RegistersLength;
    ULONG InterruptVector;
    KIRQL InterruptIrql;
    BOOLEAN InterruptLevelSensitive;
    BOOLEAN InterruptFound;
    PVOID Core;
    PUCHAR Registers;
    const POWERVR_PLATFORM *Platform;
    PVOID PlatformRegisters;
    USHORT Branch;
    USHORT Version;
    USHORT NumberOfScalableUnits;
    USHORT Config;
    PVOID Segment;
    PHYSICAL_ADDRESS SegmentPhysical;
    KSPIN_LOCK FenceLock;
    KDPC CompletionDpc;
    ULONG SubmittedFence;
    ULONG CompletedFence;
    BOOLEAN Powered;
    BOOLEAN Started;
};

#define POWERVR_CONTEXT_MAGIC   0x52564350UL

typedef struct _POWERVR_CONTEXT
{
    ULONG Magic;
    PPOWERVR_ADAPTER Adapter;
    ULONG NodeOrdinal;
} POWERVR_CONTEXT, *PPOWERVR_CONTEXT;

typedef struct _POWERVR_DEVICE
{
    ULONG Magic;
    PPOWERVR_ADAPTER Adapter;
    HANDLE hDevice;
    PVOID File;
} POWERVR_DEVICE, *PPOWERVR_DEVICE;

extern const POWERVR_PLATFORM PowerVrK1Platform;

FORCEINLINE
ULONG
PowerVrRead32(_In_ PPOWERVR_ADAPTER Adapter, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Adapter->Registers + Offset));
}

FORCEINLINE
ULONG64
PowerVrRead64(_In_ PPOWERVR_ADAPTER Adapter, _In_ ULONG Offset)
{
    ULONG64 Value;

    KeMemoryBarrier();
    Value = *(volatile ULONG64 *)(Adapter->Registers + Offset);
    KeMemoryBarrier();
    return Value;
}

FORCEINLINE
VOID
PowerVrWrite32(_In_ PPOWERVR_ADAPTER Adapter, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Adapter->Registers + Offset), Value);
}

FORCEINLINE
VOID
PowerVrWrite64(_In_ PPOWERVR_ADAPTER Adapter, _In_ ULONG Offset, _In_ ULONG64 Value)
{
    KeMemoryBarrier();
    *(volatile ULONG64 *)(Adapter->Registers + Offset) = Value;
    KeMemoryBarrier();
}
