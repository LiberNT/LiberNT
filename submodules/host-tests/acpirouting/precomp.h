/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host build environment for the ACPI PCI interrupt routing provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <acpi.h>

typedef void VOID, *PVOID;
typedef unsigned char UCHAR, *PUCHAR, KIRQL;
typedef unsigned short USHORT;
typedef unsigned int ULONG, *PULONG;

#define NTAPI
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _In_reads_opt_(x)
#define C_ASSERT(e) _Static_assert(e, #e)
#define FIELD_OFFSET(t, f) ((ULONG)offsetof(t, f))
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define DPRINT(...) ((void)0)
#define DPRINT1(...) ((void)0)
#define PASSIVE_LEVEL 0
#define NonPagedPool 0
#define PagedPool 1
#define PAGED_CODE() ((void)0)
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define RtlCopyMemory memcpy
#define RtlZeroMemory(d, n) memset((d), 0, (n))

extern int HostTestFailures;
#define ASSERT(x) do { if (!(x)) { printf("assertion failed: %s\n", #x); HostTestFailures++; } } while (0)

#define PCI_MAX_DEVICES 32
#define PCI_MAX_FUNCTION 8
#define PCI_INVALID_VENDORID 0xFFFF
#define PCI_MULTIFUNCTION 0x80
#define PCI_BRIDGE_TYPE 0x01
#define PCI_CARDBUS_BRIDGE_TYPE 0x02

typedef struct _PCI_COMMON_CONFIG
{
    USHORT VendorID;
    USHORT DeviceID;
    USHORT Command;
    USHORT Status;
    UCHAR RevisionID;
    UCHAR ProgIf;
    UCHAR SubClass;
    UCHAR BaseClass;
    UCHAR CacheLineSize;
    UCHAR LatencyTimer;
    UCHAR HeaderType;
    UCHAR BIST;
    union
    {
        struct
        {
            ULONG BaseAddresses[2];
            UCHAR PrimaryBus;
            UCHAR SecondaryBus;
            UCHAR SubordinateBus;
        } type1;
    } u;
} PCI_COMMON_CONFIG;
C_ASSERT(FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType) == 0x0E);
C_ASSERT(FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.SecondaryBus) == 0x19);

#define HAL_ACPI_POLARITY_HIGH 0
#define HAL_ACPI_POLARITY_LOW 1
#define HAL_ACPI_POLARITY_BOTH 3
#define HAL_ACPI_TRIGGER_EDGE 0
#define HAL_ACPI_TRIGGER_LEVEL 1

typedef BOOLEAN (NTAPI *PHAL_ACPI_PCI_ROUTE_QUERY)(ULONG Segment, UCHAR Bus, UCHAR Device, UCHAR Function,
                                                  UCHAR Pin, PULONG Gsi, PUCHAR Polarity, PUCHAR TriggerMode);

typedef struct _HAL_ACPI_PCI_ROUTE_ENTRY
{
    ULONG Segment;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Pin;
    ULONG Gsi;
    UCHAR Polarity;
    UCHAR TriggerMode;
} HAL_ACPI_PCI_ROUTE_ENTRY;

KIRQL KeGetCurrentIrql(VOID);
PVOID ExAllocatePoolWithTag(int PoolType, size_t Size, ULONG Tag);
VOID ExFreePoolWithTag(PVOID Block, ULONG Tag);
VOID NTAPI HalpRegisterPciRouteQuery(PHAL_ACPI_PCI_ROUTE_QUERY Provider);
ACPI_STATUS BuspGetCurrentResources(ACPI_HANDLE Handle, ACPI_BUFFER *Buffer);
ACPI_STATUS BuspGetIrqRoutingTable(ACPI_HANDLE Handle, ACPI_BUFFER *Buffer);
int acpi_pci_link_init(VOID);
void acpi_pci_link_exit(VOID);
