/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 mappings of PCI configuration space
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <halarch.h>

#define HALP_ARM_CONFIG_BUS_SIZE 0x100000ULL
#define HALP_ARM_CONFIG_MAX_BUSES 64

typedef struct _HALP_ARM_CONFIG_BUS
{
    ULONGLONG Physical;
    PUCHAR Virtual;
} HALP_ARM_CONFIG_BUS, *PHALP_ARM_CONFIG_BUS;

/*
 * ARMv7 has no permanent alias of physical memory, and configuration space is
 * read at any IRQL. The megabyte of a bus is mapped the first time it is
 * reached, which the bus driver does at PASSIVE_LEVEL, and stays mapped.
 * Entries are only added: Count is published after the entry it covers.
 */
static HALP_ARM_CONFIG_BUS HalpArmConfigBuses[HALP_ARM_CONFIG_MAX_BUSES];
static volatile LONG HalpArmConfigBusCount;
static KSPIN_LOCK HalpArmConfigLock;

static
PUCHAR
HalpArmFindConfigBus(
    _In_ ULONGLONG Physical,
    _In_ LONG Count)
{
    LONG Index;

    ASSERT(Count <= HALP_ARM_CONFIG_MAX_BUSES);
    for (Index = 0; Index < Count; Index++)
    {
        if (HalpArmConfigBuses[Index].Physical == Physical)
            return HalpArmConfigBuses[Index].Virtual;
    }

    return NULL;
}

PVOID
HalpArchMapConfigSpace(
    _In_ PHYSICAL_ADDRESS PhysicalPage)
{
    ULONGLONG Bus = (ULONGLONG)PhysicalPage.QuadPart & ~(HALP_ARM_CONFIG_BUS_SIZE - 1);
    ULONG Offset = (ULONG)((ULONGLONG)PhysicalPage.QuadPart - Bus);
    PHYSICAL_ADDRESS BusAddress;
    PUCHAR Virtual, Mapped;
    KIRQL OldIrql;
    LONG Count;

    ASSERT((PhysicalPage.QuadPart & (PAGE_SIZE - 1)) == 0);

    Count = ReadAcquire(&HalpArmConfigBusCount);
    Virtual = HalpArmFindConfigBus(Bus, Count);
    if (Virtual)
        return Virtual + Offset;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return NULL;

    BusAddress.QuadPart = (LONGLONG)Bus;
    Mapped = MmMapIoSpace(BusAddress, (SIZE_T)HALP_ARM_CONFIG_BUS_SIZE, MmNonCached);
    if (!Mapped)
        return NULL;

    KeAcquireSpinLock(&HalpArmConfigLock, &OldIrql);
    Count = HalpArmConfigBusCount;
    Virtual = HalpArmFindConfigBus(Bus, Count);
    if (!Virtual && (Count < HALP_ARM_CONFIG_MAX_BUSES))
    {
        HalpArmConfigBuses[Count].Physical = Bus;
        HalpArmConfigBuses[Count].Virtual = Mapped;
        WriteRelease(&HalpArmConfigBusCount, Count + 1);
        Virtual = Mapped;
        Mapped = NULL;
    }
    KeReleaseSpinLock(&HalpArmConfigLock, OldIrql);

    if (Mapped)
        MmUnmapIoSpace(Mapped, (SIZE_T)HALP_ARM_CONFIG_BUS_SIZE);
    return Virtual ? Virtual + Offset : NULL;
}
