/*
 * PROJECT:     LiberNT SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StarFive JH7110 DesignWare MMC clock, reset and slot glue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"
#include "hardware.h"

#define NDEBUG
#include <debug.h>

#define JH7110_SYSCRG_BASE          0x13020000ULL
#define JH7110_SYSCRG_SIZE          0x400
#define JH7110_SYSCRG_RESET_ASSERT  0x2F8
#define JH7110_SYSCRG_RESET_STATUS  0x308
#define JH7110_SYSCLK_BUS_ROOT      5
#define JH7110_SYSCLK_AXI_CFG0      7
#define JH7110_CLK_ENABLE           (1UL << 31)
#define JH7110_CLK_MUX_SHIFT        24
#define JH7110_CLK_MUX_MASK         0xFUL
#define JH7110_CLK_DIV_MASK         0xFFFFFFUL
#define JH7110_OSC_HZ               24000000ULL
#define JH7110_PLL2_HZ              1188000000ULL

static ULONG
SdBusJh7110Cell(_In_reads_bytes_(4) const UCHAR *Bytes)
{
    return ((ULONG)Bytes[0] << 24) | ((ULONG)Bytes[1] << 16) | ((ULONG)Bytes[2] << 8) | Bytes[3];
}

static BOOLEAN
SdBusJh7110Query(_In_ HANDLE Key, _In_z_ PCWSTR Name, _Out_writes_bytes_(Size) PUCHAR Buffer,
                 _In_ ULONG Size, _Out_ PULONG Length)
{
    UCHAR Storage[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 64];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    UNICODE_STRING ValueName;
    ULONG Result;

    *Length = 0;
    RtlInitUnicodeString(&ValueName, Name);
    if (!NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information,
                                    sizeof(Storage), &Result)) ||
        Information->DataLength > Size)
        return FALSE;
    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    *Length = Information->DataLength;
    return TRUE;
}

static ULONG
SdBusJh7110ReadClock(_In_ PUCHAR SysCrg, _In_ ULONG Id)
{
    return READ_REGISTER_ULONG((PULONG)(SysCrg + Id * sizeof(ULONG)));
}

static VOID
SdBusJh7110EnableClock(_In_ PUCHAR SysCrg, _In_ ULONG Id)
{
    PULONG Register = (PULONG)(SysCrg + Id * sizeof(ULONG));

    WRITE_REGISTER_ULONG(Register, READ_REGISTER_ULONG(Register) | JH7110_CLK_ENABLE);
}

static BOOLEAN
SdBusJh7110ReleaseReset(_In_ PUCHAR SysCrg, _In_ ULONG Id)
{
    PULONG Assert = (PULONG)(SysCrg + JH7110_SYSCRG_RESET_ASSERT + (Id / 32) * sizeof(ULONG));
    PULONG Status = (PULONG)(SysCrg + JH7110_SYSCRG_RESET_STATUS + (Id / 32) * sizeof(ULONG));
    ULONG Mask = 1UL << (Id % 32);
    ULONG Elapsed;

    WRITE_REGISTER_ULONG(Assert, READ_REGISTER_ULONG(Assert) & ~Mask);
    for (Elapsed = 0; Elapsed < 1000; Elapsed += 10)
    {
        if (READ_REGISTER_ULONG(Status) & Mask)
            return TRUE;
        KeStallExecutionProcessor(10);
    }
    return FALSE;
}

static ULONG
SdBusJh7110CardClockHz(_In_ PUCHAR SysCrg, _In_ ULONG CardClockId)
{
    ULONG Root = SdBusJh7110ReadClock(SysCrg, JH7110_SYSCLK_BUS_ROOT);
    ULONG Axi = SdBusJh7110ReadClock(SysCrg, JH7110_SYSCLK_AXI_CFG0) & JH7110_CLK_DIV_MASK;
    ULONG Card = SdBusJh7110ReadClock(SysCrg, CardClockId) & JH7110_CLK_DIV_MASK;
    ULONGLONG RootHz = ((Root >> JH7110_CLK_MUX_SHIFT) & JH7110_CLK_MUX_MASK) ? JH7110_PLL2_HZ : JH7110_OSC_HZ;

    if (Axi == 0 || Card == 0)
        return 0;
    return (ULONG)(RootHz / Axi / Card);
}

NTSTATUS
SdBusJh7110Attach(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PHYSICAL_ADDRESS Address;
    UCHAR Value[64];
    PUCHAR SysCrg;
    HANDLE Key;
    ULONG Length, BusClockId, CardClockId, ResetId;
    NTSTATUS Status;

    Status = IoOpenDeviceRegistryKey(FdoExtension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Key);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    if (SdBusJh7110Query(Key, L"clocks", Value, sizeof(Value), &Length) && Length >= 16)
    {
        BusClockId = SdBusJh7110Cell(Value + 4);
        CardClockId = SdBusJh7110Cell(Value + 12);
        if (SdBusJh7110Query(Key, L"resets", Value, sizeof(Value), &Length) && Length >= 8)
        {
            ResetId = SdBusJh7110Cell(Value + 4);
            Status = STATUS_SUCCESS;
        }
    }
    FdoExtension->DwMmcFifoDepth = 0;
    if (SdBusJh7110Query(Key, L"fifo-depth", Value, sizeof(Value), &Length) && Length >= 4)
        FdoExtension->DwMmcFifoDepth = SdBusJh7110Cell(Value);
    FdoExtension->DwMmcBusWidth = 1;
    if (SdBusJh7110Query(Key, L"bus-width", Value, sizeof(Value), &Length) && Length >= 4)
        FdoExtension->DwMmcBusWidth = SdBusJh7110Cell(Value);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;

    Address.QuadPart = JH7110_SYSCRG_BASE;
    SysCrg = MmMapIoSpace(Address, JH7110_SYSCRG_SIZE, MmNonCached);
    if (!SysCrg)
        return STATUS_INSUFFICIENT_RESOURCES;
    SdBusJh7110EnableClock(SysCrg, BusClockId);
    SdBusJh7110EnableClock(SysCrg, CardClockId);
    if (!SdBusJh7110ReleaseReset(SysCrg, ResetId))
        Status = STATUS_IO_TIMEOUT;
    FdoExtension->DwMmcBusHz = SdBusJh7110CardClockHz(SysCrg, CardClockId);
    MmUnmapIoSpace(SysCrg, JH7110_SYSCRG_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;
    if (FdoExtension->DwMmcBusHz == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    FdoExtension->HostType = SdBusHostDwmmc;
    FdoExtension->NonRemovable = TRUE;
    DPRINT1("SdBusJh7110Attach: DesignWare MMC, %lu Hz controller clock, %lu-bit bus\n",
            FdoExtension->DwMmcBusHz, FdoExtension->DwMmcBusWidth);
    return STATUS_SUCCESS;
}
