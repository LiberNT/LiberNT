/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB2 host bring-up of the Apple silicon Type-C PHY
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_ATCPHY_REGION
{
    PUCHAR Base;
    ULONG Size;
} APPLE_ATCPHY_REGION, *PAPPLE_ATCPHY_REGION;

typedef struct _APPLE_ATCPHY
{
    ULONG Node;
    APPLE_ATCPHY_REGION Core;
    APPLE_ATCPHY_REGION Axi2af;
    APPLE_ATCPHY_REGION Usb2Phy;
    APPLE_ATCPHY_REGION PipeHandler;
    ULONG TunableEntries[3];
    BOOLEAN HasAusPllTop;
} APPLE_ATCPHY, *PAPPLE_ATCPHY;

NTSTATUS
NTAPI
AppleAtcphyOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG ConsumerNode,
    _Out_ PAPPLE_ATCPHY Phy);

VOID
NTAPI
AppleAtcphyClose(
    _Inout_ PAPPLE_ATCPHY Phy);

NTSTATUS
NTAPI
AppleAtcphyStartUsb2Host(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_ATCPHY Phy);

NTSTATUS
NTAPI
AppleAtcphySelectDummyPipe(
    _Inout_ PAPPLE_ATCPHY Phy);

NTSTATUS
NTAPI
AppleAtcphyStopUsb2Host(
    _Inout_ PAPPLE_ATCPHY Phy);
