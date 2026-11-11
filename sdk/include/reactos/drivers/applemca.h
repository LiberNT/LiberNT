/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon multi-channel audio (MCA) serial ports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_MCA
{
    PUCHAR Clusters;
    ULONG ClustersSize;
    PUCHAR Switch;
    ULONG SwitchSize;
    ULONG ClusterCount;
} APPLE_MCA, *PAPPLE_MCA;

NTSTATUS
NTAPI
AppleMcaOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_MCA Mca);

VOID
NTAPI
AppleMcaClose(
    _Inout_ PAPPLE_MCA Mca);

NTSTATUS
NTAPI
AppleMcaConfigureTx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Slots,
    _In_ ULONG SlotWidth,
    _In_ ULONG Channels,
    _In_ ULONG SampleBits);

NTSTATUS
NTAPI
AppleMcaConfigureRx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Slots,
    _In_ ULONG SlotWidth,
    _In_ ULONG Channels,
    _In_ ULONG SampleBits,
    _In_ ULONG PortMask);

NTSTATUS
NTAPI
AppleMcaRoutePort(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Port,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable);

VOID
NTAPI
AppleMcaEnableClocks(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable);

VOID
NTAPI
AppleMcaFollowPortClocks(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ ULONG Port,
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
AppleMcaResetTx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster);

VOID
NTAPI
AppleMcaEnableTx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
AppleMcaResetRx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster);

VOID
NTAPI
AppleMcaEnableRx(
    _In_ PAPPLE_MCA Mca,
    _In_ ULONG Cluster,
    _In_ BOOLEAN Enable);
